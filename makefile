CFLAGS=-g -Wall -Wpedantic
LDLIBS=-lnettle

all: websocksy

websocksy: websocksy.c websocksy.h ws_proto.c
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@

clean:
	rm websocksy
