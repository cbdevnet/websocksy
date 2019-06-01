#include "network.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/*
 * Create a file descriptor connected to a network socket peer.
 * Client sockets will be connected, listening sockets will be bound/listened.
 * Returns -1 in case of failure, a valid fd otherwise.
 */
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

/*
 * Create a file descriptor connected to a unix socket peer.
 * Client sockets will be connected, listening sockets will be bound.
 * Returns -1 in case of failure, a valid fd otherwise.
 */
int network_socket_unix(char* path, int socktype, int listener){
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	int fd = socket(AF_UNIX, socktype, 0), flags;

	if(fd < 0){
		fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
		return -1;
	}

	//set nonblocking
	flags = fcntl(fd, F_GETFL, 0);
	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0){
		fprintf(stderr, "Failed to set socket nonblocking: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	strncpy(addr.sun_path, path, (strlen(path) < (sizeof(addr.sun_path) - 1)) ? strlen(path) : (sizeof(addr.sun_path) - 1));

	if(listener){
		unlink(path);
		if(bind(fd, (struct sockaddr*) &addr, sizeof(addr))){
			fprintf(stderr, "Failed to bind %s: %s\n", path, strerror(errno));
			close(fd);
			return -1;
		}

		if(listen(fd, SOMAXCONN)){
			fprintf(stderr, "Failed to listen on %s: %s\n", path, strerror(errno));
			close(fd);
			return -1;
		}

		return fd;
	}

	//connect clients
	if(connect(fd, (struct sockaddr*) &addr, sizeof(addr))){
		fprintf(stderr, "Failed to connect to %s: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * Send arbitrary data over multiple writes if necessary.
 * Returns 0 on success
 */
int network_send(int fd, uint8_t* data, size_t length){
	//TODO probably should introduce send buffering at some point
	ssize_t total = 0, sent;
	while(total < length){
		sent = send(fd, data + total, length - total, 0);
		if(sent < 0){
			fprintf(stderr, "Failed to send: %s\n", strerror(errno));
			return 1;
		}
		total += sent;
	}
	return 0;
}

/*
 * Send string data over multiple writes if necessary.
 * Returns 0 on success
 */
int network_send_str(int fd, char* data){
	return network_send(fd, (uint8_t*) data, strlen(data));
}
