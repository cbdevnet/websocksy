#include <stdio.h>

#include "../websocksy.h"

static int64_t framing_fixedlen(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	size_t* frame_size = framing_data ? (size_t*) (*framing_data) : NULL;

	if(data && !frame_size){
		frame_size = calloc(1, sizeof(size_t));
		if(!frame_size){
			fprintf(stderr, "Failed to allocate memory\n");
			return -1;
		}

		*frame_size = strtoul(config, NULL, 0);
		*framing_data = frame_size;
	}
	else if(!data && frame_size){
		free(*framing_data);
		*framing_data = NULL;
	}

	if(!(*frame_size)){
		return length;
	}

	if(length >= *frame_size){
		return *frame_size;
	}

	return 0;
}

static void __attribute__((constructor)) init(){
	core_register_framing("fixedlength", framing_fixedlen);
}
