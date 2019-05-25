#include <stdint.h>
#include <stdlib.h>

/* Socket interface convenience functions */
int network_socket(char* host, char* port, int socktype, int listener);
int network_send(int fd, uint8_t* data, size_t length);
int network_send_str(int fd, char* data);
