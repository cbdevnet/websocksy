#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "websocksy.h"
#include "config.h"
#include "plugin.h"

/* Configuration file parser state */
static enum /*_config_file_section*/ {
	cfg_main,
	cfg_backend
} config_section = cfg_main;

/* Evaluate a single line within a configuration file */
static int config_file_line(ws_config* config, char* key, char* value, size_t line_no){
	if(!strcmp(key, "port")){
		free(config->port);
		config->port = strdup(value);
	}
	else if(!strcmp(key, "listen")){
		free(config->host);
		config->host = strdup(value);
	}
	else if(!strcmp(key, "backend")){
		//clean up the previously registered backend
		if(config->backend.cleanup){
			config->backend.cleanup();
		}
		//load the backend plugin
		if(plugin_backend_load(PLUGINS, value, &(config->backend))){
			return 1;
		}
		if(config->backend.init() != WEBSOCKSY_API_VERSION){
			fprintf(stderr, "Loaded backend %s was built for a different API version\n", value);
			return 1;
		}
	}
	return 0;
}

/* Read and parse a configuration file */
int config_parse_file(ws_config* config, char* filename){
	ssize_t line_current;
	size_t line_alloc = 0, line_no = 1, key_len;
	char* line = NULL, *key, *value;
	FILE* input = fopen(filename, "r");
	if(!input){
		fprintf(stderr, "Failed to open %s as configuration file\n", filename);
		return 1;
	}

	for(line_current = getline(&line, &line_alloc, input); line_current >= 0; line_current = getline(&line, &line_alloc, input)){
		if(!strncmp(line, "[core]", 6)){
			config_section = cfg_main;
		}
		else if(!strncmp(line, "[backend]", 9)){
			config_section = cfg_backend;
		}
		else if(line_current > 0){
			//right-trim newlines & spaces from value
			for(line_current--; line_current && isspace(line[line_current]); line_current--){
				line[line_current] = 0;
			}

			if(line_current > 0){
				//left-trim key
				for(key = line; *key && isspace(key[0]); key++){
				}

				value = strchr(key, '=');
				if(value){
					*value = 0;
					//left-trim value
					for(value++; *value && isspace(value[0]); value++){
					}

					//right-trim key
					for(key_len = strlen(key) - 1; *key && key_len && isspace(key[key_len]); key_len--){
						key[key_len] = 0;
					}

					switch(config_section){
						case cfg_main:
							if(config_file_line(config, key, value, line_no)){
								free(line);
								fclose(input);
								return 1;
							}
							break;
						case cfg_backend:
							if(config->backend.config(key, value)){
								fprintf(stderr, "Backend configuration failed in %s line %lu\n", filename, line_no);
								free(line);
								fclose(input);
								return 1;
							}
							break;
					}
				}
			}
		}
		line_no++;
	}

	free(line);
	fclose(input);
	return 0;
}

/* Parse an argument list */
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
				config_file_line(config, "port", argv[u + 1], 0);
				break;
			case 'l':
				config_file_line(config, "listen", argv[u + 1], 0);
				break;
			case 'b':
				config_file_line(config, "backend", argv[u + 1], 0);
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
