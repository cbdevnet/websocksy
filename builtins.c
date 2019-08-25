#include <string.h>
#include <ctype.h>
#include <stdio.h>

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
	if(valid && opcode){
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

/*
 * The `separator` framing function waits until a variable-length separator is found in the stream and sends all 
 * data up to and including that separator as binary frame. The configuration string is used as the separator, with
 * the escape sequences \r, \t, \n, \0, \f, \\ being recognized as their ASCII expressions. Arbitrary bytes can be
 * specified hexadecimally using the syntax \x<hexbyte>
 */
typedef struct /*_separator_framing_config*/ {
	size_t length;
	uint8_t* separator;
} framing_separator_config;

int64_t framing_separator(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* framing_config){
	size_t u, p = 0;
	unsigned hex;
	framing_separator_config* config = framing_data ? *framing_data : NULL;

	if(data && !config){
		//parse configuration
		config = calloc(1, sizeof(framing_separator_config));
		config->separator = (uint8_t*) strdup(framing_config);
		for(u = 0; config->separator[u]; u++){
			config->separator[p] = config->separator[u];
			if(config->separator[u] == '\\'){
				switch(config->separator[u + 1]){
					case 0:
						u--;
						//fall through
					case '0':
						config->separator[p] = 0;
						break;
					case 't':
						config->separator[p] = '\t';
						break;
					case 'n':
						config->separator[p] = '\n';
						break;
					case 'f':
						config->separator[p] = '\f';
						break;
					case 'r':
						config->separator[p] = '\r';
						break;
					case '\\':
						config->separator[p] = '\\';
						break;
					case 'x':
						if(!isxdigit(config->separator[u + 2])
								|| !isxdigit(config->separator[u + 3])){
							fprintf(stderr, "Prematurely terminated hex byte sequence in separator framing function\n");
							free(config->separator);
							free(config);
							return -1;
						}
						sscanf((char*) (config->separator + u + 3), "%02x", &hex);
						config->separator[p] = hex;
						u += 2;
				}
				u++;
			}
			p++;
		}
		config->length = p;
		if(framing_data){
			*framing_data = config;
		}
	}
	else if(!data && config){
		//free parsed configuration
		free(config->separator);
		config->separator = NULL;
		config->length = 0;
		free(config);
		return 0;
	}

	if(config && config->length){
		for(u = 0; u < last_read && (last_read - u) >= config->length; u++){
			if(!memcmp(data + (length - last_read) + u, config->separator, config->length)){
				return (length - last_read) + u + config->length;
			}
		}
	}
	else{
		return length;
	}
	return 0;
}

/*
 * The `newline` framing function waits until a newline sequence is found in the buffer and sends all data up to and
 * including the newline as text frame, if the data is detected as valid UTF-8. Otherwise, a binary frame is used.
 * The configuration string may be one of
 * 	* crlf
 * 	* lfcr
 * 	* lf
 * 	* cr
 */
int64_t framing_newline(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* framing_config){
	int64_t bytes = 0;
	char* expression = "\\r\\n";
	
	if(data && (!framing_data || !(*framing_data))){
		if(!strcmp(framing_config, "crlf")){
			expression = "\\r\\n";
		}
		if(!strcmp(framing_config, "lfcr")){
			expression = "\\n\\r";
		}
		if(!strcmp(framing_config, "lf")){
			expression = "\\n";
		}
		if(!strcmp(framing_config, "cr")){
			expression = "\\r";
		}
		bytes = framing_separator(data, length, last_read, opcode, framing_data, expression);
	}
	else{
		bytes = framing_separator(data, length, last_read, opcode, framing_data, NULL);
	}

	//this should never happen
	if(bytes < 0){
		return length;
	}

	return framing_auto(data, bytes, 0, opcode, NULL, NULL);
}
