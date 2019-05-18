#include <stdint.h>
#include <stdlib.h>
#include <nettle/sha1.h>
#include <nettle/base64.h>

#define WS_MAX_LINE 16384

typedef enum {
	ws_new = 0,
	ws_http,
	ws_open,
	ws_closed
} ws_state;

//RFC Section 5.2
typedef enum {
	ws_frame_continuation = 0,
	ws_frame_text = 1,
	ws_frame_binary = 2,
	ws_frame_close = 8,
	ws_frame_ping = 9,
	ws_frame_pong = 10
} ws_operation;

//RFC Section 7.4.1
typedef enum {
	ws_close_http = 100,
	ws_close_normal = 1000,
	ws_close_shutdown = 1001,
	ws_close_proto = 1002,
	ws_close_data = 1003,
	ws_close_format = 1007,
	ws_close_policy = 1008,
	ws_close_limit = 1009,
	ws_close_unexpected = 1011
} ws_close_reason;

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
