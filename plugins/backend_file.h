#include "../websocksy.h"

uint64_t init();
uint64_t configure(char* key, char* value);
ws_peer_info query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws);
void cleanup();
