#include "backend_file.h"
#include <string.h>
#include <stdio.h>

//FIXME allocated this statically because i dont want to do it properly right now tbh
#define BACKEND_FILE_MAX_PATH 8192

static char* backend_path = NULL;

size_t expressions = 0;
static char** expression = NULL;

uint64_t init(){
	//initialize the backend path to empty to be able to use it without problems later
	backend_path = strdup("");

	if(!backend_path){
		fprintf(stderr, "Failed to allocate memory\n");
		return 0;
	}
	return WEBSOCKSY_API_VERSION;
}

uint64_t configure(char* key, char* value){
	if(!strcmp(key, "path")){
		free(backend_path);
		backend_path = strdup(value);
		if(!backend_path){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}

		if(strlen(backend_path) && backend_path[strlen(backend_path) - 1] == '/'){
			backend_path[strlen(backend_path) - 1] = 0;
		}
		return 0;
	}
	else if(!strcmp(key, "expression")){
		expression = realloc(expression, (expressions + 1) * sizeof(char*));
		if(!expression){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		expression[expressions] = strdup(value);
		if(!expression[expressions]){
			fprintf(stderr, "Failed to allocate memory\n");
			return 1;
		}
		expressions++;
		return 0;
	}

	fprintf(stderr, "Unknown backend configuration option %s\n", key);
	return 1;
}

static int expression_replace(char* buffer, size_t buffer_length, size_t variable_length, char* content, size_t content_length){
	//check whether the replacement fits
	if(variable_length < content_length && strlen(buffer) + (content_length - variable_length) >= buffer_length){
		fprintf(stderr, "Expression replacement buffer overrun\n");
		return 1;
	}

	//move data after the replacement
	memmove(buffer + content_length, buffer + variable_length, strlen(buffer) - variable_length + 1);

	//insert replacement
	memcpy(buffer, content, content_length);
	
	return 0;
}

static int expression_resolve(char* template, size_t length, char* endpoint, size_t headers, ws_http_header* header){
	size_t u, index_len, p, value_len, variable_len;
	char* index, *value;

	for(u = 0; template[u]; u++){
		index_len = 0;
		variable_len = 0;
		value = NULL;
		if(template[u] == '%'){
			if(!strncmp(template + u, "%endpoint%", 10)){
				if(strlen(endpoint) < 1){
					return 1;
				}

				//rtrim slash so test and test/ are the same file
				if(strlen(endpoint) >= 2 && endpoint[strlen(endpoint) - 1] == '/'){
					endpoint[strlen(endpoint) - 1] = 0;
				}

				//replace with sanitized endpoint string
				for(p = 0; endpoint[p]; p++){
					if(endpoint[p] == '/'){
						endpoint[p] = '_';
					}
				}

				value = endpoint + 1;
				value_len = p - 1;
				variable_len = 10;
			}
			else if(!strncmp(template + u, "%cookie:", 8)){
				//scan cookie values
				index = template + u + 8;
				for(; index[index_len] && index[index_len] != '%'; index_len++){
				}
				if(!index[index_len]){
					fprintf(stderr, "Unterminated expression variable: %s\n", index);
					return 1;
				}

				//find the cookie header
				for(p = 0; p < headers; p++){
					if(!strcmp(header[p].tag, "Cookie")){
						value = header[p].value;
						break;
					}
				}

				//no cookie header, fail the expression
				if(p == headers){
					return 1;
				}

				do{
					//ensure min length of cookie name
					if(strlen(value) < index_len + 1){
						return 1;
					}
					//check if cookie found
					if(value[index_len] == '='
							&& !strncmp(value, index, index_len)){
						value += index_len + 1;
						for(value_len = 0; value[value_len] && value[value_len] != ';'; value_len++){
						}
						break;
					}
					//skip to next cookie
					for(; *value && strncmp(value, "; ", 2); value++){
					}
					if(*value){
						value += 2;
					}
				} while(*value);

				if(!*value){
					return 1;
				}

				variable_len = 8 + index_len + 1;
			}
			else if(!strncmp(template + u, "%header:", 8)){
				//scan headers
				index = template + u + 8;
				for(; index[index_len] && index[index_len] != '%'; index_len++){
				}
				if(!index[index_len]){
					fprintf(stderr, "Unterminated expression variable: %s\n", index);
					return 1;
				}
				//find the correct header
				for(p = 0; p < headers; p++){
					if(strlen(header[p].tag) == index_len
							&& !strncmp(header[p].tag, index, index_len)){
						break;
					}
				}

				//no such header -> fail the expression
				if(p == headers){
					return 1;
				}

				value = header[p].value;
				value_len = strlen(value);
				variable_len = 8 + index_len + 1;
			}

			//perform replacement
			if(value && expression_replace(template + u, length - u, variable_len, value, value_len)){
				fprintf(stderr, "Expression replacement failed\n");
				return 1;
			}
			if(value){
				//skip the inserted value
				u += value_len - 1;
			}
		}
	}

	return 0;
}

ws_peer_info query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws){
	size_t u, p, line_alloc = 0;
	ssize_t line_length = 0;
	char* line = NULL, *components[3];
	FILE* input = NULL;
	char target_path[BACKEND_FILE_MAX_PATH];
	ws_peer_info peer = {
		.transport = peer_transport_detect,
		.protocol = protocols
	};

	for(u = 0; u < expressions; u++){
		//evaluate the current expression to find a path
		snprintf(target_path, sizeof(target_path), "%s/%s", backend_path, expression[u]);
		if(expression_resolve(target_path + strlen(backend_path) + 1, sizeof(target_path) - strlen(backend_path) - 1, endpoint, headers, header)){
			continue;
		}

		//check whether the file exists
		input = fopen(target_path, "r");
		if(!input){
			continue;
		}

		//read it
		for(line_length = getline(&line, &line_alloc, input); line_length >= 0; line_length = getline(&line, &line_alloc, input)){
			memset(components, 0, sizeof(components));
			//TODO rtrim line

			//read lines of host subproto framing framing-config
			components[0] = strchr(line, ' ');
			if(protocols && !components[0]){
				continue;
			}

			//terminate host
			components[0][0] = 0;
			components[0]++;

			//find framing, terminate subprotocol
			components[1] = strchr(components[0], ' ');
			if(components[1]){
				components[1][0] = 0;
				components[1]++;

				//find find framing config, terminate framing
				components[2] = strchr(components[1], ' ');
				if(components[2]){
					components[2][0] = 0;
					components[2]++;
				}
			}

			//find a match for any indicated protocol
			for(p = 0; p < protocols; p++){
				if(!strcmp(components[0], protocol[p])){
					peer.protocol = p;
					break;
				}
			}

			//for '*' use the first available protocol
			if(components[0] && !strcmp(components[0], "*")){
				peer.protocol = 0;
			}

			//the current line does not match any indicated protocols
			if(protocols && peer.protocol == protocols){
				continue;
			}

			peer.host = strdup(line);
			peer.framing = core_framing(components[1]);
			peer.framing_config = components[2] ? strdup(components[2]) : NULL;
			break;
		}
		fclose(input);

		//if peer found, break
		if(peer.host){
			break;
		}
	}

	free(line);
	return peer;
}

void cleanup(){
	size_t u;

	for(u = 0; u < expressions; u++){
		free(expression[u]);
	}
	free(expression);
	expression = NULL;
	expressions = 0;

	free(backend_path);
	backend_path = NULL;
}
