/* Core configuration storage */
typedef struct /*_websocksy_config*/ {
	char* host;
	char* port;
	time_t ping_interval;
	ws_backend backend;
} ws_config;

/* Configuration parsing functions */
int config_parse_file(ws_config* config, char* filename);
int config_parse_arguments(ws_config* config, int argc, char** argv);
