#include <stdio.h>
#include <string.h>
#include <endian.h>

#include "../websocksy.h"

typedef struct /*_framing_config*/ {
	uint32_t offset;
	int32_t fixed;
	uint8_t endian;
} dynamic32_config_t;

static int64_t framing_dynamic32(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	size_t u;
	uint32_t* size_p, size = 0;
	//get the current config if set
	dynamic32_config_t* conncfg = (*framing_data) ? ((dynamic32_config_t*) (*framing_data)) : NULL;

	//configure framing for this connection
	if(data && !conncfg){
		conncfg = calloc(1, sizeof(dynamic32_config_t));
		if(!conncfg){
			fprintf(stderr, "Failed to allocate memory\n");
			return -1;
		}

		//parse config string
		for(u = 0; config[u];){
			if(!strncmp(config + u, "offset=", 7)){
				conncfg->offset = strtoul(config + u + 7, NULL, 0);
			}
			else if(!strncmp(config + u, "static=", 7)){
				conncfg->fixed = strtoul(config + u + 7, NULL, 0);
			}
			else if(!strncmp(config + u, "endian=", 7)){
				conncfg->endian = 0;
				if(!strncmp(config + u + 7, "big", 3)){
					conncfg->endian = 1;
				}
			}

			//skip to next item
			for(; config[u] && config[u] != ','; u++){
			}
		}

		//store configuration
		*framing_data = conncfg;
	}
	//clean up configuration
	else if(!data && conncfg){
		free(*framing_data);
		*framing_data = NULL;
	}

	if(length >= conncfg->offset + 4){
		//read size field
		size_p = (uint32_t*) (data + conncfg->offset);
		size = le32toh(*size_p);
		if(conncfg->endian){
			size = be32toh(*size_p);
		}

		if(length >= conncfg->offset + 4 + conncfg->fixed + size){
			return conncfg->offset + 4 + conncfg->fixed + size;
		}
	}

	return 0;
}

static void __attribute__((constructor)) init(){
	core_register_framing("dynamic32", framing_dynamic32);
}
