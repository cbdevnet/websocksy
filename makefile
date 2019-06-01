.PHONY: all clean plugins
PLUGINPATH=plugins/

CFLAGS=-g -Wall -Wpedantic -DPLUGINS=\"$(PLUGINPATH)\"
LDLIBS=-lnettle -ldl

OBJECTS=builtins.o network.o websocket.o plugin.o

all: websocksy

plugins:
	$(MAKE) -C plugins

websocksy: websocksy.c websocksy.h $(OBJECTS) plugins
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(OBJECTS)

clean:
	$(RM) $(OBJECTS)
	$(RM) websocksy
