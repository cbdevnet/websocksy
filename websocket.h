#include "websocksy.h"

/* WebSocket connection handling functions */
int ws_close(websocket* ws, ws_close_reason code, char* reason);
int ws_accept(int listen_fd, time_t current_time);
int ws_send_frame(websocket* ws, ws_operation opcode, uint8_t* data, size_t len);
int ws_data(websocket* ws);
