#include <stdint.h>
#include <stdlib.h>

#define WS_MAX_LINE 16384

typedef enum {
	ws_new = 0,
	ws_http,
	ws_rfc6455
} ws_state;

struct {
	char* host;
	char* port;
	struct {
		char* name;
	} backend;
} config = {
	.host = "::",
	.port = "8001",
	.backend.name = "internal"
};

typedef struct {
	char* name;
	int fd;
} ws_proto;

typedef struct /*_web_socket*/ {
	uint8_t read_buffer[WS_MAX_LINE];
	size_t read_buffer_offset;
	ws_state state;

	int fd;
	size_t protocols;
	ws_proto* protocol;
} websocket;
