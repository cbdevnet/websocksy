#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <errno.h>

#include "websocket.h"
#include "network.h"

#define RFC6455_MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FRAME_HEADER_LEN 16

#define WS_FLAG_FIN 0x80
#define WS_GET_FIN(a) ((a & WS_FLAG_FIN) >> 7)
#define WS_GET_RESERVED(a) ((a & 0xE0) >> 4)
#define WS_GET_OP(a) ((a & 0x0F))
#define WS_GET_MASK(a) ((a & 0x80) >> 7)
#define WS_GET_LEN(a) ((a & 0x7F))

/* 
 * Close and shut down a WebSocket connection, including a connected
 * peer stream. Frees all resources associated with either connection.
 */
int ws_close(websocket* ws, ws_close_reason code, char* reason){
	size_t p;
	ws_peer_info empty_peer = {
		0
	};

	if(ws->state == ws_open && reason){
		//send close frame
		//FIXME this should prepend the status code to the reason
		ws_send_frame(ws, ws_frame_close, (uint8_t*) reason, strlen(reason));
	}
	else if(ws->state == ws_http
			&& code == ws_close_http
			&& reason){
		//send http response
		network_send_str(ws->ws_fd, "HTTP/1.1 ");
		network_send_str(ws->ws_fd, reason);
		network_send_str(ws->ws_fd, "\r\n\r\n");
	}
	ws->state = ws_closed;

	if(ws->ws_fd >= 0){
		close(ws->ws_fd);
		ws->ws_fd = -1;
	}

	if(ws->peer_fd >= 0){
		close(ws->peer_fd);
		ws->peer_fd = -1;
		//clean up framing data
		if(ws->peer_framing_data){
			ws->peer.framing(NULL, 0, 0, NULL, &(ws->peer_framing_data), ws->peer.framing_config);
			ws->peer_framing_data = NULL;
		}
	}

	for(p = 0; p < ws->headers; p++){
		free(ws->header[p].tag);
		ws->header[p].tag = NULL;
		free(ws->header[p].value);
		ws->header[p].value = NULL;
	}
	ws->headers = 0;

	for(p = 0; p < ws->protocols; p++){
		free(ws->protocol[p]);
	}
	free(ws->protocol);
	ws->protocol = NULL;
	ws->protocols = 0;

	ws->read_buffer_offset = 0;
	ws->peer_buffer_offset = 0;

	free(ws->request_path);
	ws->request_path = NULL;

	free(ws->socket_key);
	ws->socket_key = NULL;

	ws->websocket_version = 0;
	ws->want_upgrade = 0;

	free(ws->peer.host);
	free(ws->peer.port);
	free(ws->peer.framing_config);
	ws->peer = empty_peer;

	return 0;
}

/* Accept a new WebSocket connection */
int ws_accept(int listen_fd, time_t current_time){
	websocket ws = {
		.ws_fd = accept(listen_fd, NULL, NULL),
		.peer_fd = -1,
		.last_event = current_time
	};

	return client_register(&ws);
}

/* Handle data in the NEW state (expecting a HTTP negotiation) */
static int ws_handle_new(websocket* ws){
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

/* Handle end of HTTP header data and upgrade the connection */
static int ws_upgrade_http(websocket* ws){
	if(ws->websocket_version == 13
			&& ws->socket_key
			&& ws->want_upgrade == 3){

		//find and connect peer
		if(client_connect(ws)){
			ws_close(ws, ws_close_http, "500 Peer connection failed");
			return 0;
		}

		if(network_send_str(ws->ws_fd, "HTTP/1.1 101 Upgrading\r\n")
				|| network_send_str(ws->ws_fd, "Upgrade: websocket\r\n")
				|| network_send_str(ws->ws_fd, "Connection: Upgrade\r\n")){
			ws_close(ws, ws_close_http, NULL);
			return 0;
		}

		//calculate the websocket accept key, which for some reason is defined (RFC 4.2.2.5.4) as
		//base64(sha1(concat(trim(client-key), "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")))
		//requiring not one but 2 unnecessarily complex operations
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
		if(network_send_str(ws->ws_fd, "Sec-WebSocket-Accept: ")
					|| network_send_str(ws->ws_fd, ws_accept_key)){
			ws_close(ws, ws_close_http, NULL);
			return 0;
		}

		//acknowledge selected protocol
		if(ws->peer.protocol < ws->protocols){
			if(network_send_str(ws->ws_fd, "Sec-WebSocket-Protocol: ")
						|| network_send_str(ws->ws_fd, ws->protocol[ws->peer.protocol])
						|| network_send_str(ws->ws_fd, "\r\n")){
				ws_close(ws, ws_close_http, NULL);
				return 0;
			}
		}

		ws->state = ws_open;
		if(network_send_str(ws->ws_fd, "\r\n")){
			ws_close(ws, ws_close_http, NULL);
			return 0;
		}
		return 0;
	}
	//RFC 4.2.2.4: An unsupported version must be answered with HTTP 426
	if(ws->websocket_version != 13){
		ws_close(ws, ws_close_http, "426 Unsupported protocol version");
		return 0;
	}
	return 1;
}

/* Handle incoming HTTP header lines */
static int ws_handle_http(websocket* ws){
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
	else if(!strcmp(header, "Sec-WebSocket-Protocol")){
		//parse websocket protocol offers
		for(value = strtok(value, ","); value; value = strtok(NULL, ",")){
			//ltrim
			for(; *value && isspace(*value); value++){
			}

			//rtrim
			for(p = strlen(value) - 1; p >= 0 && isspace(value[p]); p--){
				value[p] = 0;
			}

			for(p = 0; p < ws->protocols; p++){
				if(!strcmp(ws->protocol[p], value)){
					break;
				}
			}

			//add new protocol
			if(p == ws->protocols){
				ws->protocol = realloc(ws->protocol, (ws->protocols + 1) * sizeof(char*));
				if(!ws->protocol){
					fprintf(stderr, "Failed to allocate memory\n");
					return 1;
				}
				ws->protocol[ws->protocols] = strdup(value);
				ws->protocols++;
			}
		}
	}
	else if(ws->headers < WS_HEADER_LIMIT){
		ws->header[ws->headers].tag = strdup(header);
		ws->header[ws->headers].value = strdup(value);
		ws->headers++;
	}
	else{
		//limit the number of stored headers to prevent abuse
		//ws_close(ws, ws_close_http, "500 Header limit");
	}
	return 0;
}

//returns bytes handled
static size_t ws_frame(websocket* ws){
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

	//calculate the payload length from one of 3 cases (RFC 5.2)
	//could've used a uint64 and be done with it...
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

	/*fprintf(stderr, "Incoming websocket data: %s %s OP %02X LEN %u %lu\n",
			WS_GET_FIN(ws->read_buffer[0]) ? "FIN" : "CONT",
			WS_GET_MASK(ws->read_buffer[1]) ? "MASK" : "CLEAR",
			WS_GET_OP(ws->read_buffer[0]),
			WS_GET_LEN(ws->read_buffer[1]),
			payload_length);*/

	//handle data
	switch(WS_GET_OP(ws->read_buffer[0])){
		case ws_frame_text:
			//fprintf(stderr, "Text payload: %.*s\n", (int) payload_length, (char*) payload);
		case ws_frame_binary:
			//forward to peer
			if(ws->peer_fd >= 0){
				fprintf(stderr, "WS -> Peer %lu bytes\n", payload_length);
				if(network_send(ws->peer_fd, payload, payload_length)){
					ws_close(ws, ws_close_unexpected, "Failed to forward");
				}
			}
			break;
		case ws_frame_close:
			ws_close(ws, ws_close_normal, "Client requested termination");
			break;
		case ws_frame_ping:
			if(ws_send_frame(ws, ws_frame_pong, payload, payload_length)){
				ws_close(ws, ws_close_unexpected, "Failed to send pong");
			}
			break;
		case ws_frame_pong:
			//TODO keep-alive pings
			break;
		default:
			//unknown frame type received
			fprintf(stderr, "Unknown WebSocket opcode %02X in frame\n", WS_GET_OP(ws->read_buffer[0]));
			ws_close(ws, ws_close_proto, "Invalid opcode");
			break;
	}

	return ((payload - ws->read_buffer) + payload_length);
}

/* Construct and send a WebSocket frame */
int ws_send_frame(websocket* ws, ws_operation opcode, uint8_t* data, size_t len){
	fprintf(stderr, "Peer -> WS %lu bytes (%02X)\n", len, opcode);
	uint8_t frame_header[WS_FRAME_HEADER_LEN];
	size_t header_bytes = 2;
	uint16_t* payload_len16 = (uint16_t*) (frame_header + 2);
	uint64_t* payload_len64 = (uint64_t*) (frame_header + 2);

	//set up the basic frame header
	frame_header[0] = WS_FLAG_FIN | opcode;
	if(len <= 125){
		frame_header[1] = len;
	}
	else if(len <= 0xFFFF){
		frame_header[1] = 126;
		*payload_len16 = htobe16(len);
		header_bytes += 2;
	}
	else{
		frame_header[1] = 127;
		*payload_len64 = htobe64(len);
		header_bytes += 8;
	}

	if(network_send(ws->ws_fd, frame_header, header_bytes)
			|| network_send(ws->ws_fd, data, len)){
		return 1;
	}

	return 0;
}

/* Handle incoming data on a WebSocket client */
int ws_data(websocket* ws){
	ssize_t bytes_read, n, bytes_left = sizeof(ws->read_buffer) - ws->read_buffer_offset;
	int rv = 0;

	bytes_read = recv(ws->ws_fd, ws->read_buffer + ws->read_buffer_offset, bytes_left - 1, 0);
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
			break;
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
