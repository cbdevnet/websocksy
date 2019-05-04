#include <stdint.h>
#include <stdlib.h>

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
	int fd;
	size_t protocols;
	ws_proto* protocol;
} websocket;
