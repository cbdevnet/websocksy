#include <stdint.h>
#include <stdlib.h>
#include <nettle/sha1.h>
#include <nettle/base64.h>

/* Version defines */
#define WEBSOCKSY_API_VERSION 1
#define WEBSOCKSY_VERSION "0.1"

/* HTTP/WS read buffer size & limit */
#define WS_MAX_LINE 16384
/* Maximum number of HTTP headers to accept */
#define WS_HEADER_LIMIT 10

/*
 * State machine for WebSocket connections
 */
typedef enum {
	ws_new = 0, /* Initial state */
	ws_http, /* HTTP Headers */
	ws_open, /* Upgrade performed, forwarding */
	ws_closed /* Close frame sent */
} ws_state;

/*
 * WebSocket frame opcodes
 * RFC Section 5.2
 */
typedef enum {
	ws_frame_continuation = 0,
	ws_frame_text = 1,
	ws_frame_binary = 2,
	ws_frame_close = 8,
	ws_frame_ping = 9,
	ws_frame_pong = 10
} ws_operation;

/*
 * WebSocket close reasons / response codes
 * RFC Section 7.4.1
 */
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

/*
 * HTTP header split into tag and value
 */
typedef struct /*_ws_http_header*/ {
	char* tag;
	char* value;
} ws_http_header;

/*
 * Peer stream framing function
 *
 * Since the WebSocket protocol transfers its payload using discrete frames, unlike the peer's
 * underlying TCP/Unix sockets (UDP is a different matter and may be framed directly), we need
 * a method to indicate whether a complete frame to be forwarded has been received from the peer
 * and with what WebSocket frame type to forward it (binary/text). Since this is largely
 * protocol-dependent, this functionality needs to be user-extendable.
 *
 * We do however provide some default framing functions:
 * 	* binary: Always forward all reads as binary frames
 * 	* auto: Based on the content, forward every read result as binary/text
 * 	* separator: Separate binary frames on a sequence of bytes
 * 	* newline: Forward text frames separated by newlines (\r\n)
 *
 * The separator function is called once for every succesful read from the peer socket and called
 * again when it indicates a frame boundary but there is still data in the buffer.
 */
typedef int64_t (*ws_framing)(void);

/*
 * Peer connection model
 */
typedef struct /*_ws_peer_info*/ {
	/* Peer protocol data */
	int transport;
	char* host;
	char* port;

	/* Framing function for this peer */
	ws_framing framing;

	/* WebSocket subprotocol indication index*/
	size_t protocol;
} ws_peer_info;

/*
 * Core connection model
 */
typedef struct /*_web_socket*/ {
	/* WebSocket state & data */
	int ws_fd;
	uint8_t read_buffer[WS_MAX_LINE];
	size_t read_buffer_offset;
	ws_state state;

	/* HTTP request headers */
	size_t headers;
	ws_http_header header[WS_HEADER_LIMIT];

	/* WebSocket parameters */
	char* request_path;
	unsigned websocket_version;
	char* socket_key;
	unsigned want_upgrade;

	/* WebSocket indicated subprotocols*/
	size_t protocols;
	char** protocol;

	/* Peer data */
	ws_peer_info peer;
	int peer_fd;
} websocket;

/*
 * Peer discovery backend
 *
 * Used to dynamically select the peer based on parameters supplied by the WebSocket connection.
 * The backend maps WebSocket characteristics (such as the endpoint used, the supported protocols and
 * HTTP client headers) to a TCP/UDP/Unix socket peer endpoint using some form of user-configurable
 * provider (such as databases, files, crystal balls or sheer guesses).
 *
 * The return value is a structure containing the destination address and transport to be connected to
 * the Web Socket, as well as the indicated subprotocol to use (or none, if set to the provided maximum
 * number of protocols).
 * The fields within the structure should be allocated with `calloc` and will be free'd by websocky
 * after use.
 */
typedef ws_peer_info (*ws_backend)(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws);
