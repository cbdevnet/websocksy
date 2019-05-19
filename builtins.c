static ws_peer_info default_peer = {
	.transport = peer_tcp_client,
	.host = "localhost",
	.port = "5900"
};

//TODO backend configuration
ws_peer_info backend_defaultpeer_query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws){
	//return a copy of the default peer
	ws_peer_info peer = default_peer;
	peer.host = (default_peer.host) ? strdup(default_peer.host) : NULL;
	peer.port = (default_peer.port) ? strdup(default_peer.port) : NULL;

	//TODO backend protocol discovery
	peer.protocol = protocols;
	return peer;
}

int64_t framing_auto(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, char* config){
	//TODO implement auto framer
	return length;
}

int64_t framing_binary(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, char* config){
	return length;
}

int64_t framing_separator(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, char* config){
	//TODO implement separator framer
	return length;
}

int64_t framing_newline(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, char* config){
	//TODO implement separator framer
	return length;
}
