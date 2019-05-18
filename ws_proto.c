#define RFC6455_MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WS_GET_FIN(a) ((a & 0x80) >> 7)
#define WS_GET_RESERVED(a) ((a & 0xE0) >> 4)
#define WS_GET_OP(a) ((a & 0x0F))
#define WS_GET_MASK(a) ((a & 0x80) >> 7)
#define WS_GET_LEN(a) ((a & 0x7F))

static size_t socks = 0;
static websocket* sock = NULL;

int ws_close(websocket* ws, ws_close_reason code, char* reason){
	size_t p;

	if(ws->state == ws_open && reason){
		//TODO send close frame
	}
	ws->state = ws_closed;

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
			&& ws->socket_key
			&& ws->want_upgrade == 3){
		if(network_send_str(ws->fd, "HTTP/1.1 101 Upgrading\r\n")
				|| network_send_str(ws->fd, "Upgrade: websocket\r\n")
				|| network_send_str(ws->fd, "Connection: Upgrade\r\n")){
			ws_close(ws, ws_close_http, NULL);
			return 0;
		}

		//calculate the websocket key which for some reason is defined as
		//base64(sha1(concat(trim(client-key), "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")))
		//which requires not one but 2 unnecessarily complex operations
		size_t encode_offset = 0;
		struct sha1_ctx ws_accept_ctx;
		struct base64_encode_ctx ws_accept_encode;
		uint8_t ws_accept_digest[SHA1_DIGEST_SIZE];
		char ws_accept_key[BASE64_ENCODE_LENGTH(SHA1_DIGEST_SIZE) + 3] = "";
		sha1_init(&ws_accept_ctx);
		sha1_update(&ws_accept_ctx, strlen(ws->socket_key), (uint8_t*) ws->socket_key);
		sha1_update(&ws_accept_ctx, strlen(RFC6455_MAGIC_KEY), (uint8_t*) RFC6455_MAGIC_KEY);
		sha1_digest(&ws_accept_ctx, sizeof(ws_accept_digest), (uint8_t*) &ws_accept_digest);
		base64_encode_init(&ws_accept_encode);
		encode_offset = base64_encode_update(&ws_accept_encode, (uint8_t*) ws_accept_key, SHA1_DIGEST_SIZE, ws_accept_digest);
		encode_offset += base64_encode_final(&ws_accept_encode, (uint8_t*) ws_accept_key + encode_offset);
		memcpy(ws_accept_key + encode_offset, "\r\n\0", 3);

		//send websocket accept key
		if(network_send_str(ws->fd, "Sec-WebSocket-Accept: ")
					|| network_send_str(ws->fd, ws_accept_key)){
			ws_close(ws, ws_close_http, NULL);
			return 0;
		}

		//TODO Sec-Websocket-Protocol
		//TODO find/connect peer

		ws->state = ws_open;
		if(network_send_str(ws->fd, "\r\n")){
			ws_close(ws, ws_close_http, NULL);
			return 0;
		}
		return 0;
	}
	//TODO RFC 4.2.2.4: An unsupported version must be answered with HTTP 426
	return 1;
}

int ws_handle_http(websocket* ws){
	char* header, *value;
	ssize_t p;

	if(!ws->read_buffer[0]){
		return ws_upgrade_http(ws);
	}
	else if(isspace(ws->read_buffer[0])){
		//i hate header folding
		ws_close(ws, ws_close_http, "500 Header folding");
		return 0;
	}
	else{
		header = (char*) ws->read_buffer;
		value = strchr(header, ':');
		if(!value){
			ws_close(ws, ws_close_http, "500 Header format");
			return 0;
		}
		*value = 0;
		value++;
		for(; isspace(*value); value++){
		}
	}

	//RFC 4.2.1 checks
	if(!strcmp(header, "Sec-WebSocket-Version")){
		ws->websocket_version = strtoul(value, NULL, 10);
	}
	else if(!strcmp(header, "Sec-WebSocket-Key")){
		//right-trim the key
		for(p = strlen(value) - 1; p >= 0 && isspace(value[p]); p--){
			value[p] = 0;
		}
		ws->socket_key = strdup(value);
	}
	else if(!strcmp(header, "Upgrade") && !strcasecmp(value, "websocket")){
		ws->want_upgrade |= 1;
	}
	else if(!strcmp(header, "Connection") && strstr(xstr_lower(value), "upgrade")){
		ws->want_upgrade |= 2;
	}
	//TODO parse websocket protocol offers
	return 0;
}

//returns bytes handled
size_t ws_frame(websocket* ws){
	size_t u;
	uint64_t payload_length = 0;
	uint16_t* payload_len16 = (uint16_t*) (ws->read_buffer + 2);
	uint64_t* payload_len64 = (uint64_t*) (ws->read_buffer + 2);
	uint8_t* masking_key = NULL, *payload = ws->read_buffer + 2;

	//need at least the header bits
	if(ws->read_buffer_offset < 2){
		return 0;
	}

	if(WS_GET_RESERVED(ws->read_buffer[0])){
		//reserved bits set without any extensions
		//RFC 5.2 says we MUST close the connection
		//ignoring it for now
	}

	//calculate the payload length (could've used a uint64 and be done with it...)
	//TODO test this for the bigger frames
	payload_length = WS_GET_LEN(ws->read_buffer[1]);
	if(WS_GET_MASK(ws->read_buffer[1])){
		if(ws->read_buffer_offset < 6){
			return 0;
		}
		masking_key = ws->read_buffer + 2;
		payload = ws->read_buffer + 6;
	}

	if(payload_length == 126){
		//16-bit payload length
		if(ws->read_buffer_offset < 4){
			return 0;
		}
		payload_length = htobe16(*payload_len16);
		payload = ws->read_buffer + 4;
		if(WS_GET_MASK(ws->read_buffer[1])){
			if(ws->read_buffer_offset < 8){
				return 0;
			}
			masking_key = ws->read_buffer + 4;
			payload = ws->read_buffer + 8;
		}
	}
	else if(payload_length == 127){
		//64-bit payload length
		if(ws->read_buffer_offset < 10){
			return 0;
		}
		payload_length = htobe64(*payload_len64);
		payload = ws->read_buffer + 10;
		if(WS_GET_MASK(ws->read_buffer[1])){
			if(ws->read_buffer_offset < 14){
				return 0;
			}
			masking_key = ws->read_buffer + 10;
			payload = ws->read_buffer + 14;
		}
	}

	//check for complete WS frame
	if(ws->read_buffer_offset < (payload - ws->read_buffer) + payload_length){
		//fprintf(stderr, "Incomplete payload: offset %lu, want %lu\n", ws->read_buffer_offset, (payload - ws->read_buffer) + payload_length);
		return 0;
	}

	//RFC Section 5.1: If the client sends an unmasked frame, close the connection
	if(!WS_GET_MASK(ws->read_buffer[1])){
		ws_close(ws, ws_close_proto, "Unmasked client frame");
		return 0;
	}

	//unmask data
	if(WS_GET_MASK(ws->read_buffer[1])){
		for(u = 0; u < payload_length; u++){
			payload[u] = payload[u] ^ masking_key[u % 4];
		}
	}

	//TODO handle fragmentation
	//TODO handle control frames within fragmented frames

	fprintf(stderr, "Incoming websocket data: %s %s OP %02X LEN %u %lu\n",
			WS_GET_FIN(ws->read_buffer[0]) ? "FIN" : "CONT",
			WS_GET_MASK(ws->read_buffer[1]) ? "MASK" : "CLEAR",
			WS_GET_OP(ws->read_buffer[0]),
			WS_GET_LEN(ws->read_buffer[1]),
			payload_length);

	//handle data
	switch(WS_GET_OP(ws->read_buffer[0])){
		case ws_frame_text:
			fprintf(stderr, "Text payload: %.*s\n", (int) payload_length, (char*) payload);
		case ws_frame_binary:
			//TODO forward to peer
			break;
		case ws_frame_close:
			ws_close(ws, ws_close_normal, "Client requested termination");
			break;
		case ws_frame_ping:
			break;
		case ws_frame_pong:
			break;
		default:
			//TODO unknown frame type received
			break;
	}

	return ((payload - ws->read_buffer) + payload_length);
}

int ws_data(websocket* ws){
	ssize_t bytes_read, n, bytes_left = sizeof(ws->read_buffer) - ws->read_buffer_offset;
	int rv = 0;

	bytes_read = recv(ws->fd, ws->read_buffer + ws->read_buffer_offset, bytes_left - 1, 0);
	if(bytes_read < 0){
		fprintf(stderr, "Failed to receive from websocket: %s\n", strerror(errno));
		ws_close(ws, ws_close_unexpected, NULL);
		return 0;
	}
	else if(bytes_read == 0){
		//client closed connection
		ws_close(ws, ws_close_unexpected, NULL);
		return 0;
	}

	//terminate new data
	ws->read_buffer[ws->read_buffer_offset + bytes_read] = 0;
	
	switch(ws->state){
		case ws_new:
		case ws_http:
			//scan for newline, handle line
			for(n = 0; n < bytes_read - 1; n++){
				if(!strncmp((char*) ws->read_buffer + ws->read_buffer_offset + n, "\r\n", 2)){
					//terminate line
					ws->read_buffer[ws->read_buffer_offset + n] = 0;

					if(ws->state == ws_new){
						ws_handle_new(ws);
					}
					else{
						ws_handle_http(ws);
					}

					//remove from buffer
					bytes_read -= (n + 2);
					memmove(ws->read_buffer, ws->read_buffer + ws->read_buffer_offset + n + 2, bytes_read);
					ws->read_buffer_offset = 0;

					//restart from the beginning
					n = -1;
				}
			}
			//update read buffer offset
			ws->read_buffer_offset = bytes_read;
			break;
		case ws_open:
			ws->read_buffer_offset += bytes_read;
			for(n = ws_frame(ws); n > 0 && ws->read_buffer_offset > 0; n = ws_frame(ws)){
				memmove(ws->read_buffer, ws->read_buffer + n, ws->read_buffer_offset - n);
				ws->read_buffer_offset -= n;
			}
		//this should never be reached, as ws_close also closes the client fd
		case ws_closed:
			fprintf(stderr, "This should not have happened\n");
			break;
	}

	//disconnect spammy clients
	if(sizeof(ws->read_buffer) - ws->read_buffer_offset < 2){
		fprintf(stderr, "Disconnecting misbehaving client\n");
		ws_close(ws, ws_close_limit, "Receive size limit exceeded");
		return 0;
	}
	return rv;
}

void ws_cleanup(){
	size_t n;
	for(n = 0; n < socks; n++){
		ws_close(sock + n, ws_close_shutdown, "Shutting down");
	}

	free(sock);
	sock = NULL;
	socks = 0;
}
