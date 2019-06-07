#include <string.h>
#include <stdio.h>

#include "websocksy.h"
#include "config.h"
#include "plugin.h"

static enum /*_config_file_section*/ {
	cfg_main,
	cfg_backend
} config_section = cfg_main;

int config_parse_file(ws_config* config, char* filename){
	return 1;
}

int config_parse_arguments(ws_config* config, int argc, char** argv){
	size_t u;
	char* option = NULL, *value = NULL;

	//if exactly one argument, treat it as config file
	if(argc == 1){
		return config_parse_file(config, argv[0]);
	}

	if(argc % 2){
		return 1;
	}

	for(u = 0; u < argc; u += 2){
		if(argv[u][0] != '-'){
			return 1;
		}
		switch(argv[u][1]){
			case 'p':
				config->port = argv[u + 1];
				break;
			case 'l':
				config->host = argv[u + 1];
				break;
			case 'b':
				//clean up the previously registered backend
				if(config->backend.cleanup){
					config->backend.cleanup();
				}
				//load the backend plugin
				if(plugin_backend_load(PLUGINS, argv[u + 1], &(config->backend))){
					return 1;
				}
				if(config->backend.init() != WEBSOCKSY_API_VERSION){
					fprintf(stderr, "Loaded backend %s was built for a different API version\n", argv[u + 1]);
					return 1;
				}
				break;
			case 'c':
				if(!strchr(argv[u + 1], '=')){
					return 1;
				}
				if(!config->backend.config){
					continue;
				}
				option = strdup(argv[u + 1]);
				value = strchr(option, '=');
				*value = 0;
				value++;
				config->backend.config(option, value);
				free(option);
				option = NULL;
				value = NULL;
				break;
		}
	}
	return 0;
}
