typedef struct /*_websocksy_config*/ {
	char* host;
	char* port;
	ws_backend backend;
} ws_config;

int config_parse_file(ws_config* config, char* filename);
int config_parse_arguments(ws_config* config, int argc, char** argv);
