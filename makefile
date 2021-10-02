.PHONY: all clean plugins
PLUGINPATH ?= plugins/

CFLAGS += -g -Wall -Wpedantic -DPLUGINS=\"$(PLUGINPATH)\"
LDLIBS = -lnettle -ldl

OBJECTS = builtins.o network.o websocket.o plugin.o config.o

all: websocksy

plugins:
	$(MAKE) -C plugins

websocksy: LDFLAGS += -Wl,-export-dynamic
websocksy: websocksy.c websocksy.h $(OBJECTS) plugins
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(OBJECTS) $(LDLIBS)

clean:
	$(RM) $(OBJECTS)
	$(RM) websocksy
