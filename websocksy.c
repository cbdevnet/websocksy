#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "websocksy.h"
#include "builtins.h"
#include "network.h"
#include "websocket.h"
#include "plugin.h"
#include "config.h"

#define DEFAULT_HOST "::"
#define DEFAULT_PORT "8001"

/* TODO
 * - TLS
 * - config file
 * - pings
 */

/* Main loop condition, to be set from signal handler */
static volatile sig_atomic_t shutdown_requested = 0;

/* Core client registry */
static size_t socks = 0;
static websocket* sock = NULL;

/* Lowercase input string in-place */
char* xstr_lower(char* in){
	size_t n;
	for(n = 0; n < strlen(in); n++){
		in[n] = tolower(in[n]);
	}
	return in;
}

/* Daemon configuration */
static ws_config config = {
	.host = DEFAULT_HOST,
	.port = DEFAULT_PORT,
	/* Assign the built-in defaultpeer backend by default */
	.backend.init = backend_defaultpeer_init,
	.backend.config = backend_defaultpeer_configure,
	.backend.query = backend_defaultpeer_query,
	.backend.cleanup = backend_defaultpeer_cleanup
};

/* Push a new client to the registry */
int client_register(websocket* ws){
	size_t n = 0;

	//try to find a slot to occupy
	for(n = 0; n < socks; n++){
		if(sock[n].ws_fd == -1){
			break;
		}
	}

	//none found, need to extend
	if(n == socks){
		sock = realloc(sock, (socks + 1) * sizeof(websocket));
		if(!sock){
			close(ws->ws_fd);
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		socks++;
	}

	sock[n] = *ws;

	return 0;
}

void client_cleanup(){
	size_t n;
	for(n = 0; n < socks; n++){ 
		 ws_close(sock + n, ws_close_shutdown, "Shutting down");
	}

	free(sock);
	sock = NULL;
	socks = 0;
}

static peer_transport client_detect_transport(char* host){
	if(!strncmp(host, "tcp://", 6)){
		memmove(host, host + 6, strlen(host) - 5);
		return peer_tcp_client;
	}
	else if(!strncmp(host, "udp://", 6)){
		memmove(host, host + 6, strlen(host) - 5);
		return peer_udp_client;
	}
	else if(!strncmp(host, "fifotx://", 9)){
		memmove(host, host + 9, strlen(host) - 8);
		return peer_fifo_tx;
	}
	else if(!strncmp(host, "fiforx://", 9)){
		memmove(host, host + 9, strlen(host) - 8);
		return peer_fifo_rx;
	}
	else if(!strncmp(host, "unix://", 7)){
		memmove(host, host + 7, strlen(host) - 6);
		return peer_unix_stream;
	}
	else if(!strncmp(host, "unix-dgram://", 13)){
		memmove(host, host + 13, strlen(host) - 12);
		return peer_unix_dgram;
	}

	fprintf(stderr, "Peer address %s does not include any known protocol identifier, guessing tcp_client\n", host);
	return peer_tcp_client;
}

static char* client_detect_port(char* host){
	size_t u;

	for(u = 0; host[u]; u++){
		if(host[u] == ':'){
			host[u] = 0;
			return strdup(host + u + 1);
		}
	}

	return NULL;
}

/* Establish peer connection for negotiated websocket */
int client_connect(websocket* ws){
	ws->peer = config.backend.query(ws->request_path, ws->protocols, ws->protocol, ws->headers, ws->header, ws);
	if(!ws->peer.host){
		//no peer provided
		return 1;
	}

	//assign default framing function if none provided
	if(!ws->peer.framing){
		ws->peer.framing = framing_auto;
	}

	//if required scan the hostname for a protocol
	if(ws->peer.transport == peer_transport_detect){
		ws->peer.transport = client_detect_transport(ws->peer.host);
	}

	if((ws->peer.transport == peer_tcp_client || ws->peer.transport == peer_udp_client)
		       && !ws->peer.port){
		ws->peer.port = client_detect_port(ws->peer.host);
		if(!ws->peer.port){
			//no port provided
			return 1;
		}
	}

	//TODO connection establishment should be async in the future
	switch(ws->peer.transport){
		case peer_tcp_client:
			ws->peer_fd = network_socket(ws->peer.host, ws->peer.port, SOCK_STREAM, 0);
			break;
		case peer_udp_client:
			ws->peer_fd = network_socket(ws->peer.host, ws->peer.port, SOCK_DGRAM, 0);
			break;
		case peer_fifo_tx:
		case peer_fifo_rx:
			//TODO implement other peer modes
			fprintf(stderr, "Peer connection mode not yet implemented\n");
			return 1;
		case peer_unix_stream:
			ws->peer_fd = network_socket_unix(ws->peer.host, SOCK_STREAM, 0);
			break;
		case peer_unix_dgram:
			ws->peer_fd = network_socket_unix(ws->peer.host, SOCK_DGRAM, 0);
			break;
		default:
			fprintf(stderr, "Invalid peer transport selected\n");
			return 1;
	}

	return (ws->peer_fd == -1) ? 1 : 0;
}

ws_framing core_framing(char* name){
	return plugin_framing(name);
}

/* Signal handler, attached to SIGINT */
static void signal_handler(int signum){
	shutdown_requested = 1;
}

/* Usage info */
static int usage(char* fn){
	fprintf(stderr, "\nwebsocksy v%s - Proxy between websockets and 'real' sockets\n", WEBSOCKSY_VERSION);
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s [-p <port>] [-l <listen address>] [-b <discovery backend>] [-c <option>=<value>]\n", fn);
	fprintf(stderr, "Arguments:\n");
	fprintf(stderr, "\t-p <port>\t\tWebSocket listen port (Current: %s, Default: %s)\n", config.port, DEFAULT_PORT);
	fprintf(stderr, "\t-l <address>\t\tWebSocket listen address (Current: %s, Default: %s)\n", config.host, DEFAULT_HOST);
	fprintf(stderr, "\t-b <backend>\t\tPeer discovery backend (Default: built-in 'defaultpeer')\n");
	fprintf(stderr, "\t-c <option>=<value>\tPass configuration options to the peer discovery backend\n");
	return EXIT_FAILURE;
}

static int ws_peer_data(websocket* ws){
	ssize_t bytes_read, bytes_left = sizeof(ws->peer_buffer) - ws->peer_buffer_offset;
	int64_t bytes_framed;
	//default to a binary frame
	ws_operation opcode = ws_frame_binary;

	bytes_read = recv(ws->peer_fd, ws->peer_buffer + ws->peer_buffer_offset, bytes_left - 1, 0);
	if(bytes_read < 0){
		fprintf(stderr, "Failed to receive from peer: %s\n", strerror(errno));
		ws_close(ws, ws_close_unexpected, "Peer connection failed");
		return 0;
	}
	else if(bytes_read == 0){
		//peer closed connection
		ws_close(ws, ws_close_normal, "Peer closed connection");
		return 0;
	}

	ws->peer_buffer[ws->peer_buffer_offset + bytes_read] = 0;

	do{
		//call the framing function
		bytes_framed = ws->peer.framing(ws->peer_buffer, ws->peer_buffer_offset + bytes_read, bytes_read, &opcode, &(ws->peer_framing_data), ws->peer.framing_config);
		if(bytes_framed > 0){
			if(bytes_framed > ws->peer_buffer_offset + bytes_read){
				ws_close(ws, ws_close_unexpected, "Internal error");
				fprintf(stderr, "Overrun by framing function, have %lu + %lu bytes, framed %lu\n", ws->peer_buffer_offset, bytes_read, bytes_framed);
				return 0;
			}
			//send the indicated n bytes to the websocket peer
			if(ws_send_frame(ws, opcode, ws->peer_buffer, bytes_framed)){
				return 1;
			}
			//copy back
			memmove(ws->peer_buffer, ws->peer_buffer + bytes_framed, (ws->peer_buffer_offset + bytes_read) - bytes_framed);
			//this should not actually happen, but it might with some weird framing functions/protocols
			if(bytes_framed < ws->peer_buffer_offset){
				ws->peer_buffer_offset -= bytes_framed;
			}
			else{
				bytes_framed -= ws->peer_buffer_offset;
				bytes_read -= bytes_framed;
				ws->peer_buffer_offset = 0;
			}
		}
		else if(bytes_framed < 0){
			//TODO handle framing errors
		}
	}
	while(bytes_framed && (ws->peer_buffer_offset + bytes_read) > 0);

	ws->peer_buffer_offset += bytes_read;
	return 0;
}

int main(int argc, char** argv){
	fd_set read_fds;
	size_t n;
	int listen_fd = -1, status, max_fd;

	//register default framing functions before parsing arguments, as they may be assigned within a backend configuration
	if(plugin_register_framing("auto", framing_auto)
			|| plugin_register_framing("binary", framing_binary)
			|| plugin_register_framing("separator", framing_separator)
			|| plugin_register_framing("newline", framing_newline)){
		fprintf(stderr, "Failed to initialize builtins\n");
		exit(EXIT_FAILURE);
	}

	//load plugin framing functions
	if(plugin_framing_load(PLUGINS)){
		fprintf(stderr, "Failed to load plugins\n");
		exit(EXIT_FAILURE);
	}

	//initialize the default backend
	if(config.backend.init() != WEBSOCKSY_API_VERSION){
		fprintf(stderr, "Failed to initialize builtin backend\n");
		exit(EXIT_FAILURE);
	}

	//parse command line arguments
	if(config_parse_arguments(&config, argc - 1, argv + 1)){
		exit(usage(argv[0]));
	}

	//open listening socket
	listen_fd = network_socket(config.host, config.port, SOCK_STREAM, 1);
	if(listen_fd < 0){
		exit(usage(argv[0]));
	}

	//attach signal handler to catch Ctrl-C
	signal(SIGINT, signal_handler);
	//ignore broken pipes when writing
	signal(SIGPIPE, SIG_IGN);

	//core loop
	while(!shutdown_requested){
		FD_ZERO(&read_fds);

		FD_SET(listen_fd, &read_fds);
		max_fd = listen_fd;

		//push all fds to the select set
		for(n = 0; n < socks; n++){
			if(sock[n].ws_fd >= 0){
				FD_SET(sock[n].ws_fd, &read_fds);
				if(max_fd < sock[n].ws_fd){
					max_fd = sock[n].ws_fd;
				}
				
				if(sock[n].peer_fd >= 0){
					FD_SET(sock[n].peer_fd, &read_fds);
					if(max_fd < sock[n].peer_fd){
						max_fd = sock[n].peer_fd;
					}
				}
			}
		}

		//block until something happens
		status = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
		if(status < 0){
			fprintf(stderr, "Failed to select: %s\n", strerror(errno));
			break;
		}
		else if(status == 0){
			//timeout in select - ignore for now
		}
		else{
			//new websocket client
			if(FD_ISSET(listen_fd, &read_fds)){
				if(ws_accept(listen_fd)){
					break;
				}
			}

			//websocket or peer data ready
			for(n = 0; n < socks; n++){
				if(sock[n].ws_fd >= 0){
					if(FD_ISSET(sock[n].ws_fd, &read_fds)){
						if(ws_data(sock + n)){
							break;
						}
					}

					if(sock[n].peer_fd >= 0 && FD_ISSET(sock[n].peer_fd, &read_fds)){
						if(ws_peer_data(sock + n)){
							break;
						}
					}
				}
			}
		}
	}

	//cleanup
	if(config.backend.cleanup){
		config.backend.cleanup();
	}
	client_cleanup();
	plugin_cleanup();
	close(listen_fd);
	return 0;
}
