CFLAGS=-g -Wall -Wpedantic
LDLIBS=-lnettle

OBJECTS=builtins.o network.o websocket.o

all: websocksy

websocksy: websocksy.c websocksy.h $(OBJECTS)
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(OBJECTS)

clean:
	$(RM) $(OBJECTS)
	$(RM) websocksy
