#include <string.h>
#include <stdio.h>

#include "websocksy.h"
#include "plugin.h"

static size_t framing_functions = 0;
static ws_framing* framing_function = NULL;
static char** framing_function_name = NULL;

int plugin_framing_load(char* path){
	//TODO load plugins
	return 1;
}

int plugin_backend_load(char* backend_requested, ws_backend* backend){
	//TODO load backend
	return 1;
}

int plugin_register_framing(char* name, ws_framing func){
	size_t u;

	for(u = 0; u < framing_functions; u++){
		if(!strcmp(framing_function_name[u], name)){
			fprintf(stderr, "Replacing framing %s\n", name);
			break;
		}
	}

	if(u == framing_functions){
		framing_function = realloc(framing_function, (framing_functions + 1) * sizeof(ws_framing));
		framing_function_name = realloc(framing_function_name, (framing_functions + 1) * sizeof(char*));
		if(!framing_function || !framing_function_name){
			fprintf(stderr, "Failed to allocate memory for framing function\n");
			return 1;
		}

		framing_function_name[u] = strdup(name);
		framing_functions++;
	}

	framing_function[u] = func;
	return 0;
}

ws_framing plugin_framing(char* name){
	size_t u;

	if(!name){
		return plugin_framing("auto");
	}

	for(u = 0; u < framing_functions; u++){
		if(!strcmp(framing_function_name[u], name)){
			return framing_function[u];
		}
	}

	//if unknown framing, return the default
	return plugin_framing("auto");
}

void plugin_cleanup(){
	size_t u;
	//TODO dlclose all plugins

	for(u = 0; u < framing_functions; u++){
		free(framing_function_name[u]);
	}
	free(framing_function);
	framing_function = NULL;
	free(framing_function_name);
	framing_function_name = 0;
	framing_function = NULL;
	framing_functions = 0;
}
