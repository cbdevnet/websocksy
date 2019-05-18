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

char* xstr_lower(char* in){
	size_t n;
	for(n = 0; n < strlen(in); n++){
		in[n] = tolower(in[n]);
	}
	return in;
}

#include "websocksy.h"
#include "network.c"
#include "ws_proto.c"

/* TODO
 * - TLS
 * - config parsing
 * - Prevent http overrun
 */

static volatile sig_atomic_t shutdown_requested = 0;

void signal_handler(int signum){
	shutdown_requested = 1;
}

int usage(char* fn){
	fprintf(stderr, "\nwebsocksy - Proxy between websockets and 'real' sockets\n");
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

	if(args_parse(argc - 1, argv + 1)){
		exit(usage(argv[0]));
	}

	//open listening socket
	listen_fd = network_socket(config.host, config.port, SOCK_STREAM, 1);
	if(listen_fd < 0){
		exit(usage(argv[0]));
	}

	signal(SIGINT, signal_handler);

	//core loop
	while(!shutdown_requested){
		FD_ZERO(&read_fds);

		FD_SET(listen_fd, &read_fds);
		max_fd = listen_fd;

		//push all fds to the select set
		for(n = 0; n < socks; n++){
			if(sock[n].fd >= 0){
				FD_SET(sock[n].fd, &read_fds);
				if(max_fd < sock[n].fd){
					max_fd = sock[n].fd;
				}
				
				if(sock[n].peer >= 0){
					FD_SET(sock[n].peer, &read_fds);
					if(max_fd < sock[n].peer){
						max_fd = sock[n].peer;
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

			//websocket & peer data
			for(n = 0; n < socks; n++){
				if(sock[n].fd >= 0){
					if(FD_ISSET(sock[n].fd, &read_fds)){
						if(ws_data(sock + n)){
							break;
						}
					}

					if(sock[n].peer >= 0 && FD_ISSET(sock[n].peer, &read_fds)){
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
