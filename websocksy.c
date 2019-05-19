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

/* TODO
 * - TLS
 * - config parsing
 * - backend config
 * - framing config / per peer?
 * - per-connection framing state
 * - framing function discovery / registry
 */

/*
 * Main loop condition, to be set from signal handler
 */
static volatile sig_atomic_t shutdown_requested = 0;

#include "websocksy.h"

/*
 * Lowercase input string in-place
 */
char* xstr_lower(char* in){
	size_t n;
	for(n = 0; n < strlen(in); n++){
		in[n] = tolower(in[n]);
	}
	return in;
}

#include "network.c"
#include "builtins.c"

/*
 * WebSocket interface & peer discovery configuration
 */
static struct {
	char* host;
	char* port;
	ws_backend backend;
} config = {
	.host = "::",
	.port = "8001",
	.backend.query = backend_defaultpeer_query
};

int connect_peer(websocket* ws){
	int rv = 1;

	ws->peer = config.backend.query(ws->request_path, ws->protocols, ws->protocol, ws->headers, ws->header, ws);
	if(!ws->peer.host || !ws->peer.port){
		//no peer provided
		return 1;
	}

	//assign default framing function if none provided
	if(!ws->peer.framing){
		ws->peer.framing = framing_auto;
	}

	switch(ws->peer.transport){
		case peer_tcp_client:
			ws->peer_fd = network_socket(ws->peer.host, ws->peer.port, SOCK_STREAM, 0);
			break;
		case peer_udp_client:
			ws->peer_fd = network_socket(ws->peer.host, ws->peer.port, SOCK_DGRAM, 0);
			break;
		case peer_tcp_server:
			//TODO implement tcp server mode
			fprintf(stderr, "TCP Server mode not yet implemented\n");
			rv = 1;
			break;
		case peer_udp_server:
			ws->peer_fd = network_socket(ws->peer.host, ws->peer.port, SOCK_DGRAM, 1);
			break;
		case peer_fifo_tx:
		case peer_fifo_rx:
		case peer_unix:
		default:
			//TODO implement other peer modes
			fprintf(stderr, "Peer connection mode not yet implemented\n");
			rv = 1;
			break;
	}

	rv = (ws->peer_fd == -1) ? 1 : 0;
	return rv;
}

#include "ws_proto.c"

/*
 * Signal handler, attached to SIGINT
 */
void signal_handler(int signum){
	shutdown_requested = 1;
}

/*
 * Usage info
 */
int usage(char* fn){
	fprintf(stderr, "\nwebsocksy v%s - Proxy between websockets and 'real' sockets\n", WEBSOCKSY_VERSION);
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s [-p <port>] [-l <listen address>] [-b <targeting backend>]\n", fn);
	return EXIT_FAILURE;
}

int args_parse(int argc, char** argv){
	//TODO
	return 0;
}

int ws_peer_data(websocket* ws){
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

	//parse command line arguments
	if(args_parse(argc - 1, argv + 1)){
		exit(usage(argv[0]));
	}

	//open listening socket
	listen_fd = network_socket(config.host, config.port, SOCK_STREAM, 1);
	if(listen_fd < 0){
		exit(usage(argv[0]));
	}

	//attach signal handler to catch Ctrl-C
	signal(SIGINT, signal_handler);

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
	ws_cleanup();
	close(listen_fd);
	return 0;
}
