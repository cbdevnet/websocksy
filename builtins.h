/* The builtin `defaultpeer` backend */
uint64_t backend_defaultpeer_init();
uint64_t backend_defaultpeer_configure(char* key, char* value);
ws_peer_info backend_defaultpeer_query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws);
void backend_defaultpeer_cleanup();

/* Built-in framing functions */
int64_t framing_auto(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config);
int64_t framing_binary(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config);
int64_t framing_separator(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config);
int64_t framing_newline(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config);
