.PHONY: all clean
PLUGINS = backend_file.so framing_fixedlength.so

CFLAGS += -fPIC -g -I../
LDFLAGS += -shared

%.so :: %.c %.h
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

%.so :: %.c
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@ $(LDFLAGS)

all: $(PLUGINS)

clean:
	$(RM) $(PLUGINS)
