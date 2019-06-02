#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <dlfcn.h>

#include "websocksy.h"
#include "plugin.h"

//cheap out because i dont want the overhead of allocating here
#define MAX_PLUGIN_PATH 4096

static size_t framing_functions = 0;
static ws_framing* framing_function = NULL;
static char** framing_function_name = NULL;

static size_t attached_libraries = 0;
static void** attached_library = NULL;

static void* plugin_attach(char* path){
	void* module = dlopen(path, RTLD_NOW);

	if(!module){
		fprintf(stderr, "Failed to load module %s\n", dlerror());
		return NULL;
	}

	attached_library = realloc(attached_library, (attached_libraries + 1) * sizeof(void*));
	if(!attached_library){
		fprintf(stderr, "Failed to allocate memory\n");
		dlclose(module);
		return NULL;
	}

	attached_library[attached_libraries] = module;
	attached_libraries++;

	return module;
}

int plugin_framing_load(char* path){
	DIR* directory = opendir(path);
	struct dirent* file = NULL;
	char plugin_path[MAX_PLUGIN_PATH] = "";

	if(strlen(path) >= sizeof(plugin_path) - 20 || strlen(path) == 0){
		fprintf(stderr, "Plugin path length exceeds limit\n");
		return 1;
	}

	if(!directory){
		fprintf(stderr, "Failed to open directory %s: %s\n", path, strerror(errno));
		return 0;
	}

	for(file = readdir(directory); file; file = readdir(directory)){
		//skip backends
		if(!strncmp(file->d_name, "backend_", 8)){
			continue;
		}
		//skip file not ending in .so
		if(strlen(file->d_name) < 4 || strcmp(file->d_name + strlen(file->d_name) - 3, ".so")){
			continue;
		}

		snprintf(plugin_path, sizeof(plugin_path), "%s%s%s", path, (path[strlen(path) - 1] == '/') ? "" : "/", file->d_name);
		if(!plugin_attach(plugin_path)){
			return 1;
		}
	}

	closedir(directory);
	return 0;
}

int plugin_backend_load(char* path, char* backend_requested, ws_backend* backend){
	char plugin_path[MAX_PLUGIN_PATH] = "";
	void* handle = NULL;

	if(strlen(path) >= sizeof(plugin_path) - 30 || strlen(path) == 0){
		fprintf(stderr, "Plugin path length exceeds limit\n");
		return 1;
	}

	snprintf(plugin_path, sizeof(plugin_path), "%s%sbackend_%s.so", path, (path[strlen(path) - 1] == '/') ? "" : "/", backend_requested);

	handle = plugin_attach(plugin_path);
	if(!handle){
		return 1;
	}

	//read backend functions into the structure
	backend->init = (ws_backend_init) dlsym(handle, "init");
	backend->config = (ws_backend_configure) dlsym(handle, "configure");
	backend->query = (ws_backend_query) dlsym(handle, "query");
	backend->cleanup = (ws_backend_cleanup) dlsym(handle, "cleanup");

	if(!backend->init || !backend->query){
		fprintf(stderr, "Backend module %s is missing required symbols\n", plugin_path);
		return 1;
	}
	return 0;
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
	
	//free allocated buffers
	for(u = 0; u < framing_functions; u++){
		free(framing_function_name[u]);
	}
	free(framing_function);
	framing_function = NULL;
	free(framing_function_name);
	framing_function_name = 0;
	framing_function = NULL;
	framing_functions = 0;

	//dlclose all plugins
	for(u = 0; u < attached_libraries; u++){
		dlclose(attached_library[u]);
	}
	free(attached_library);
	attached_libraries = 0;
}
