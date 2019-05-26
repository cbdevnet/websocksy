PLUGINPATH=plugins/

CFLAGS=-g -Wall -Wpedantic -DPLUGINS=\"$(PLUGINPATH)\"
LDLIBS=-lnettle

OBJECTS=builtins.o network.o websocket.o plugin.o

all: websocksy

websocksy: websocksy.c websocksy.h $(OBJECTS)
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(OBJECTS)

clean:
	$(RM) $(OBJECTS)
	$(RM) websocksy
