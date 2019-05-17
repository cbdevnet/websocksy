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

typedef struct /*_web_socket*/ {
	int fd;
	int peer;
	uint8_t read_buffer[WS_MAX_LINE];
	size_t read_buffer_offset;
	ws_state state;

	char* request_path;
	unsigned websocket_version;
	char* socket_key;
	unsigned want_upgrade;

	size_t protocols;
	char** protocol;
} websocket;
