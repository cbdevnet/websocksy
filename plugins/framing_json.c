#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "../websocksy.h"

static ssize_t json_length(char* data, size_t length);
static ssize_t json_length_string(char* data, size_t length);

static ssize_t json_length_array(char* data, size_t length){
	size_t current_offset = 1;
	int64_t next_length = 0;

	if(!length){
		return 0;
	}

	if(data[0] != '['){
		return -1;
	}

	do{
		//skip whitespace
		for(; current_offset < length && isspace(data[current_offset]); current_offset++){
		}

		//empty array
		if(!next_length && data[current_offset] == ']'){
			break;
		}

		//get length
		next_length = json_length(data + current_offset, length - current_offset);
		if(next_length <= 0){
			return next_length;
		}

		current_offset += next_length;
		//check for end or ,
		for(; current_offset < length && isspace(data[current_offset]); current_offset++){
		}
		//the ordering matters, check for complete array before checking for eod
		if(data[current_offset] == ']'){
			break;
		}
		if(current_offset >= length){
			return 0;
		}
		if(data[current_offset] != ','){
			break;
		}
		current_offset++;
	} while(current_offset < length);

	if(data[current_offset] == ']'){
		return current_offset + 1;
	}

	return -1;
}

static ssize_t json_length_object(char* data, size_t length){
	size_t current_offset = 1;
	int64_t next_length = 0;

	if(!length){
		return 0;
	}

	if(data[0] != '{'){
		return -1;
	}

	do{
		//skip whitespace
		for(; current_offset < length && isspace(data[current_offset]); current_offset++){
		}

		//empty object
		if(!next_length && data[current_offset] == '}'){
			break;
		}

		//get key string length
		next_length = json_length_string(data + current_offset, length - current_offset);
		if(next_length <= 0){
			return next_length;
		}

		current_offset += next_length;
		//find colon
		for(; current_offset < length && isspace(data[current_offset]); current_offset++){
		}
		if(current_offset >= length - 1){ //need one more character
			return 0;
		}
		else if(data[current_offset] != ':'){
			return -1;
		}
		current_offset++;

		//get value length
		next_length = json_length(data + current_offset, length - current_offset);
		if(next_length <= 0){
			return next_length;
		}

		current_offset += next_length;
		//check for end or ,
		for(; current_offset < length && isspace(data[current_offset]); current_offset++){
		}
		//the ordering matters, check for complete object before checking for eod
		if(data[current_offset] == '}'){
			break;
		}
		if(current_offset >= length){
			return 0;
		}
		if(data[current_offset] != ','){
			break;
		}
		current_offset++;
	} while(current_offset < length);

	if(data[current_offset] == '}'){
		return current_offset + 1;
	}
	return -1;
}

static ssize_t json_length_string(char* data, size_t length){
	size_t string_length = 0;

	if(!length){
		return 0;
	}

	if(data[0] != '"'){
		return -1;
	}

	//find terminating quotation mark not preceded by escape
	for(string_length = 1; string_length < length
				&& isprint(data[string_length])
				&& (data[string_length] != '"' || data[string_length - 1] == '\\'); string_length++){
	}

	//complete string found
	if(data[string_length] == '"' && data[string_length - 1] != '\\'){
		return string_length + 1;
	}

	//still missing data
	if(string_length >= length){
		return 0;
	}

	//anything else
	return -1;
}

static ssize_t json_length_value(char* data, size_t length){
	size_t value_length = 0;

	if(!length){
		return 0;
	}

	if(data[0] == '-' || isdigit(data[0])){
		//a number consisting of [minus] int [.int] [eE[+-]]
		//terminator (comma, space, eod, eoa, eoo)
		//since this is not a full parser, just ensure all characters are from that general set...
		for(value_length = 1; value_length < length &&
					(isdigit(data[value_length])
					|| data[value_length] == '+'
					|| data[value_length] == '-'
					|| data[value_length] == '.'
					|| tolower(data[value_length]) == 'e'); value_length++){
		}
		if(data[value_length] == ','
				|| data[value_length] == ' '
				|| data[value_length] == '}'
				|| data[value_length] == ']'){
			return value_length;
		}
		if(value_length >= length){
			if(data[value_length - 1] == '.'
					|| tolower(data[value_length - 1]) == 'e'
					|| data[value_length - 1] == '-'
					|| data[value_length - 1] == '+'){
				return 0;
			}
			//numbers can be a value on their own...
			return value_length;
		}
	}

	//match complete values
	if(length >= 4 && !strncmp(data, "null", 4)){
		return 4;
	}
	else if(length >= 4 && !strncmp(data, "true", 4)){
		return 4;
	}
	else if(length >= 5 && !strncmp(data, "false", 5)){
		return 5;
	}

	//match prefix values
	if(length < 4 && !strncmp(data, "null", length)){
		return 0;
	}
	else if(length < 4 && !strncmp(data, "true", length)){
		return 0;
	}
	else if(length < 5 && !strncmp(data, "false", length)){
		return 0;
	}

	//invalid value
	return -1;
}

static ssize_t json_length(char* data, size_t length){
	size_t total_length = 0, current_offset = 0;
	ssize_t rv = 0;

	if(!length){
		return 0;
	}

	//advance to the first non-blank
	for(current_offset = 0; current_offset < length && isspace(data[current_offset]); current_offset++){
	}

	switch(data[current_offset]){
		case '[':
			rv = json_length_array(data + current_offset, length - current_offset);
			break;
		case '{':
			rv = json_length_object(data + current_offset, length - current_offset);
			break;
		case '"':
			rv = json_length_string(data + current_offset, length - current_offset);
			break;
		default:
			//false null true
			//0-9 | - -> number
			rv = json_length_value(data + current_offset, length - current_offset);
			break;
	}

	if(rv <= 0){
		return rv;
	}

	return current_offset + rv;
}

static int64_t framing_json(uint8_t* data, size_t length, size_t last_read, ws_operation* opcode, void** framing_data, const char* config){
	ssize_t data_length = json_length(data, length);

	if(data_length > 0){
		*opcode = ws_frame_text;
		return data_length;
	}
	//incomplete
	else if(data_length == 0){
		return 0;
	}
	//failed to parse as json, just dump it
	else{
		return length;
	}
}

static void __attribute__((constructor)) init(){
	core_register_framing("json", framing_json);
}
