CFLAGS=-g -Wall -Wpedantic

all: websocksy

websocksy: websocksy.c websocksy.h ws_proto.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm websocksy
