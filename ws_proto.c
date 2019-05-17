#include <ctype.h>

static size_t socks = 0;
static websocket* sock = NULL;

int ws_close(websocket* ws){
	size_t p;

	if(ws->fd >= 0){
		close(ws->fd);
		ws->fd = -1;
	}

	if(ws->peer >= 0){
		close(ws->peer);
		ws->peer = -1;
	}

	for(p = 0; p < ws->protocols; p++){
		free(ws->protocol[p]);
	}
	ws->protocols = 0;
	ws->protocol = NULL;

	ws->read_buffer_offset = 0;

	free(ws->request_path);
	ws->request_path = NULL;

	free(ws->socket_key);
	ws->socket_key = NULL;

	ws->websocket_version = 0;
	ws->want_upgrade = 0;

	return 0;
}

int ws_accept(int listen_fd){
	size_t n = 0;

	websocket ws = {
		.fd = accept(listen_fd, NULL, NULL),
		.peer = -1
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
			return 1;
		}
		socks++;
	}

	sock[n] = ws;

	return 0;
}

int ws_handle_new(websocket* ws){
	size_t u;
	char* path, *proto;

	if(!strncmp((char*) ws->read_buffer, "GET ", 4)){
		path = (char*) ws->read_buffer + 4;
		for(u = 0; path[u] && !isspace(path[u]); u++){
		}
		path[u] = 0;
		proto = path + u + 1;
	}
	//TODO handle other methods
	else{
		fprintf(stderr, "Unknown HTTP method in request\n");
		return 1;
	}

	if(strncmp(proto, "HTTP/", 5)){
		fprintf(stderr, "Malformed HTTP initiation\n");
		return 1;
	}

	ws->state = ws_http;
	ws->request_path = strdup(path);
	return 0;
}

int ws_upgrade_http(websocket* ws){
	if(ws->websocket_version == 13
			&& ws->want_upgrade){

		return 0;
	}
	return 1;
}

int ws_handle_http(websocket* ws){
	char* header, *value;
	if(!ws->read_buffer[0]){
		return ws_upgrade_http(ws);
	}
	else if(isspace(ws->read_buffer[0])){
		//i hate header folding
		//TODO disconnect client
		return 1;
	}
	else{
		header = (char*) ws->read_buffer;
		value = strchr(header, ':');
		if(!value){
			//TODO disconnect
			return 1;
		}
		*value = 0;
		value++;
		for(; isspace(*value); value++){
		}
	}

	if(!strcmp(header, "Sec-WebSocket-Version")){
		ws->websocket_version = strtoul(value, NULL, 10);
	}
	else if(!strcmp(header, "Sec-WebSocket-Key")){
		ws->socket_key = strdup(value);
	}
	else if(!strcmp(header, "Upgrade") && !strcmp(value, "websocket")){
		ws->want_upgrade = 1;
	}
	//TODO parse websocket protocol offers
	return 0;
}

int ws_data(websocket* ws){
	ssize_t bytes_read, u, bytes_left = sizeof(ws->read_buffer) - ws->read_buffer_offset;
	int rv = 0;

	bytes_read = recv(ws->fd, ws->read_buffer + ws->read_buffer_offset, bytes_left - 1, 0);
	if(bytes_read < 0){
		fprintf(stderr, "Failed to receive from websocket: %s\n", strerror(errno));
		ws_close(ws);
		return 0;
	}
	else if(bytes_read == 0){
		//client closed connection
		ws_close(ws);
		return 0;
	}

	//terminate new data
	ws->read_buffer[ws->read_buffer_offset + bytes_read] = 0;
	
	switch(ws->state){
		case ws_new:
		case ws_http:
			//scan for newline, handle line
			for(u = 0; u < bytes_read - 1; u++){
				if(!strncmp((char*) ws->read_buffer + ws->read_buffer_offset + u, "\r\n", 2)){
					//terminate line
					ws->read_buffer[ws->read_buffer_offset + u] = 0;

					if(ws->state == ws_new){
						rv |= ws_handle_new(ws);
					}
					else{
						rv |= ws_handle_http(ws);
					}
					//TODO handle rv

					//remove from buffer
					bytes_read -= (u + 2);
					memmove(ws->read_buffer, ws->read_buffer + ws->read_buffer_offset + u + 2, bytes_read);
					ws->read_buffer_offset = 0;

					//restart from the beginning
					u = -1;
				}
			}
			break;
		//case ws_rfc6455:
			//TODO parse websocket encap, forward to peer
	}

	//update read buffer offset
	ws->read_buffer_offset = bytes_read;

	//disconnect spammy clients
	if(sizeof(ws->read_buffer) - ws->read_buffer_offset < 2){
		fprintf(stderr, "Disconnecting misbehaving client\n");
		ws_close(ws);
		return 0;
	}
	return rv;
}

void ws_cleanup(){
	size_t n;
	for(n = 0; n < socks; n++){
		ws_close(sock + n);
	}

	free(sock);
	sock = NULL;
	socks = 0;
}
