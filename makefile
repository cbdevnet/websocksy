CFLAGS=-g -Wall -Wpedantic

all: websocksy

websocksy: websocksy.c websocksy.h

clean:
	rm websocksy
