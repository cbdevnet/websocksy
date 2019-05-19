static ws_peer_info default_peer = {
	0
};

ws_peer_info backend_defaultpeer_query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws){
	//return a copy of the default peer
	ws_peer_info peer = {
		0
	};
	return peer;
}
