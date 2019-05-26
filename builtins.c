#include <string.h>

#include "websocksy.h"
#include "builtins.h"
#include "plugin.h"

#define UTF8_BYTE(a) ((a & 0xC0) == 0x80)

/*
 * The defaultpeer backend returns the same peer configured peer for any
 * incoming connection. This may be useful for testing or for configurations
 * where there is just one peer anyway.
 */

/* Global data storage for the backend */
static ws_peer_info default_peer = {0};
static char* default_peer_proto = NULL;

/* 
 * Initialization function for the defaultpeer backend
 * Heap-allocate all data so it can be safely free'd at cleanup
 */
uint64_t backend_defaultpeer_init(){
	ws_peer_info startup_peer = {
		.transport = peer_transport_detect,
		.host = (default_peer.host) ? default_peer.host : strdup("tcp://localhost"),
		.port = (default_peer.port) ? default_peer.port : strdup("5900")
	};

	default_peer = startup_peer;
	return WEBSOCKSY_API_VERSION;
}

/*
 * Configuration function for the defaultpeer backend
 */
uint64_t backend_defaultpeer_configure(char* key, char* value){
	if(!strcmp(key, "host")){
		free(default_peer.host);
		default_peer.host = strdup(value);
		default_peer.transport = peer_transport_detect;
	}
	else if(!strcmp(key, "port")){
		free(default_peer.port);
		default_peer.port = strdup(value);
	}
	else if(!strcmp(key, "protocol")){
		free(default_peer_proto);
		default_peer_proto = strdup(value);
	}
	else if(!strcmp(key, "framing")){
		default_peer.framing = plugin_framing(value);
	}
	else if(!strcmp(key, "framing-config")){
		free(default_peer.framing_config);
		default_peer.framing_config = strdup(value);
	}
	return 1;
}

/*
 * defaultpeer backend core
 * Returns the configured default peer for any incoming request and selects either
 * a matching or no subprotocol.
 */
ws_peer_info backend_defaultpeer_query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws){
	size_t p;
	//return a copy of the default peer
	ws_peer_info peer = default_peer;
	peer.host = (default_peer.host) ? strdup(default_peer.host) : NULL;
	peer.port = (default_peer.port) ? strdup(default_peer.port) : NULL;
	peer.framing_config = (default_peer.framing_config) ? strdup(default_peer.framing_config) : NULL;

	//if none set, announce none
	peer.protocol = protocols;

	if(default_peer_proto){
		for(p = 0; p < protocols; p++){
			if(!strcasecmp(protocol[p], default_peer_proto)){
				peer.protocol = p;
				break;
			}
		}

		//if any, return first
		if(!strcmp(default_peer_proto, "*")){
			peer.protocol = 0;
		}
	}
	return peer;
}

/*
 * Cleanup function for the defaultpeer backend.
 * Frees all allocated data.
 */
void backend_defaultpeer_cleanup(){
	free(default_peer.host);
	default_peer.host = NULL;
	free(default_peer.port);
	default_peer.port = NULL;
	free(default_peer.framing_config);
	default_peer.framing_config = NULL;
	free(default_peer_proto);
	default_peer_proto = NULL;
}

/*
 * The `auto` stream framing function forwards all incoming data from the peer immediately,
 * with the text frame type if the data is valid UTF8 and the binary frame type otherwise.
 */
int64_t framing_auto(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	size_t p;
	uint8_t valid = 1;
	for(p = 0; p < length; p++){
		//4-byte codepoint
		if((data[p] & 0xF8) == 0xF0){
			if((p + 3) >= length
					|| !UTF8_BYTE(data[p + 1])
					|| !UTF8_BYTE(data[p + 2])
					|| !UTF8_BYTE(data[p + 3])){
				valid = 0;
				break;
			}
			p += 3;
		}
		//3 byte codepoint
		else if((data[p] & 0xF0) == 0xE0){
			if((p + 2) >= length
					|| !UTF8_BYTE(data[p + 1])
					|| !UTF8_BYTE(data[p + 2])){
				valid = 0;
				break;
			}
			p += 2;
		}
		//2 byte codepoint
		else if((data[p] & 0xE0) == 0xC0){
			if((p + 1) >= length
					|| !UTF8_BYTE(data[p + 1])){
				valid = 0;
				break;
			}
			p++;
		}
		//not a 1 byte codepoint -> not utf8
		else if(data[p] & 0x80){
			valid = 0;
			break;
		}
	}

	//if valid utf8, send as text frame
	if(valid){
		*opcode = ws_frame_text;
	}

	return length;
}

/*
 * The `binary` peer stream framing function forwards all incoming data from the peer immediately as binary frames
 */
int64_t framing_binary(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	return length;
}

int64_t framing_separator(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	//TODO implement separator framer
	return length;
}

int64_t framing_newline(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	//TODO implement separator framer
	return length;
}
