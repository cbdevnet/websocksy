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
	ws->peer = config.backend.query(ws->request_path, ws->protocols, ws->protocol, ws->headers, ws->header, ws);
	return 0;
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
	//TODO
	return -1;
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
