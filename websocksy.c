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

#include "websocksy.h"

/* TODO
 * - TLS
 * - config parsing
 */

static size_t socks = 0;
static websocket* sock = NULL;

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

int network_socket(char* host, char* port, int socktype, int listener){
	int fd = -1, status, yes = 1, flags;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = socktype,
		.ai_flags = (listener ? AI_PASSIVE : 0)
	};
	struct addrinfo *info, *addr_it;

	status = getaddrinfo(host, port, &hints, &info);
	if(status){
		fprintf(stderr, "Failed to parse address %s port %s: %s\n", host, port, gai_strerror(status));
		return -1;
	}

	//traverse the result list
	for(addr_it = info; addr_it; addr_it = addr_it->ai_next){
		fd = socket(addr_it->ai_family, addr_it->ai_socktype, addr_it->ai_protocol);
		if(fd < 0){
			continue;
		}

		//set required socket options
		yes = 1;
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to enable SO_REUSEADDR on socket: %s\n", strerror(errno));
		}

		yes = 0;
		if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&yes, sizeof(yes)) < 0){
			fprintf(stderr, "Failed to unset IPV6_V6ONLY on socket: %s\n", strerror(errno));
		}

		//TODO loop, bcast for udp

		if(listener){
			status = bind(fd, addr_it->ai_addr, addr_it->ai_addrlen);
			if(status < 0){
				close(fd);
				continue;
			}
		}
		else{
			status = connect(fd, addr_it->ai_addr, addr_it->ai_addrlen);
			if(status < 0){
				close(fd);
				continue;
			}
		}

		break;
	}
	freeaddrinfo(info);

	if(!addr_it){
		fprintf(stderr, "Failed to create socket for %s port %s\n", host, port);
		return -1;
	}

	//set nonblocking
	flags = fcntl(fd, F_GETFL, 0);
	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0){
		fprintf(stderr, "Failed to set socket nonblocking: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	if(!listener){
		return fd;
	}

	if(socktype == SOCK_STREAM){
		status = listen(fd, SOMAXCONN);
		if(status < 0){
			fprintf(stderr, "Failed to listen on socket: %s\n", strerror(errno));
			close(fd);
			return -1;
		}
	}

	return fd;
}

int ws_accept(int listen_fd){
	size_t n = 0;
	struct sockaddr_storage sa_storage;
	socklen_t sa_length;

	websocket ws = {
		.fd = accept(listen_fd, (struct sockaddr*)&sa_storage, &sa_length)
	};

	//try to find a slot to occupy
	for(n = 0; n < socks; n++){
		if(sock[n].fd == -1){
			break;
		}
	}

	//none found, need to extend
	if(n == socks){
		sock = realloc(sock, (socks + 1) * sizeof(websocket));
		if(!sock){
			close(ws.fd);
			fprintf(stderr, "Failed to allocate memory\n");
			shutdown_requested = 1;
			return 1;
		}
		socks++;
	}

	sock[n] = ws;

	return 0;
}

int ws_data(websocket* ws){
	//TODO
	return -1;
}

int ws_peer_data(websocket* ws, size_t proto){
	//TODO
	return -1;
}

int ws_close(websocket* ws){
	size_t p;

	if(ws->fd >= 0){
		close(ws->fd);
		ws->fd = -1;
	}

	for(p = 0; p < ws->protocols; p++){
		if(ws->protocol[p].fd >= 0){
			close(ws->protocol[p].fd);
			ws->protocol[p].fd = -1;
			free(ws->protocol[p].name);
		}
	}

	return 0;
}

int main(int argc, char** argv){
	fd_set read_fds;
	size_t n, p;
	int listen_fd = -1, status, max_fd;

	if(args_parse(argc - 1, argv + 1)){
		exit(usage(argv[0]));
	}

	//open listening socket
	listen_fd = network_socket(config.host, config.port, SOCK_STREAM, 1);
	if(listen_fd < 0){
		exit(usage(argv[0]));
	}

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

				for(p = 0; p < sock[n].protocols; p++){
					if(sock[n].protocol[p].fd >= 0){
						FD_SET(sock[n].protocol[p].fd, &read_fds);
						if(max_fd < sock[n].protocol[p].fd){
							max_fd = sock[n].protocol[p].fd;
						}
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

					for(p = 0; p < sock[n].protocols; p++){
						if(sock[n].protocol[p].fd >= 0 && FD_ISSET(sock[n].protocol[p].fd, &read_fds)){
							if(ws_peer_data(sock + n, p)){
								break;
							}
						}
					}
				}
			}
		}
	}

	//cleanup
	for(n = 0; n < socks; n++){
		ws_close(sock + n);
	}
	free(sock);
	socks = 0;
	close(listen_fd);
	return 0;
}
