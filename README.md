# websocksy

`websocksy` is a highly configurable [WebSocket (RFC6455)](https://tools.ietf.org/html/rfc6455) to 'normal' networking transport (TCP/UDP/Unix sockets) bridge.
It is written in C and supports pluggable modules for bridge peer selection modules (for dynamic bridging) and stream framing.

It can be used to connect a wide variety of existing applications directly to a website, or to implement new functionality in a way that maintains compatibility
between traditional network transports and WebSockets.

## Table of contents

* [Motivation](#motivation)
* [Dynamic proxying](#dynamic-proxying)
	* [Peer discovery backends](#peer-discovery-backends)
	* [Peer stream framing](#peer-stream-framing)
* [Configuration / Usage](#configuration--usage)
	* [Command line arguments](#command-line-arguments)
	* [Configuration file](#configuration-file)
	* [Default backend](#default-backend)
	* [Plugins](#plugins)
* [Building](#building)
* [Development](#development)
	* [Peer discovery backend API](#peer-discovery-backend-api)
	* [Peer stream framing API](#peer-stream-framing-api)

## Motivation

Connecting WebSockets to 'real' sockets may seem like an easy task at first, but the WebSocket protocol has some fundamental differences to TCP and UDP.

### Framing

Data sent over WebSockets is explicitly framed - you get told how much data to expect for any one package. This is similar to UDP, which operates on Datagrams,
which are always received as one full message and carry an integrated length field.

In contrast to that, TCP operates on a 'stream' basis, where any message boundaries need to be established by an upper layer protocol, and any messages sent
may be fragmented into multiple receive operations.

Bridging data _from_ the WebSocket _to_ the network peer is thus not the problem - one can simply write out any complete message frame received.
For TCP, the other direction needs some kind of indication when to send the currently buffered data from the stream as one message to the WebSocket client.

### Frame typing

WebSocket frames contain an `opcode`, which indicates the type of data the frame contains, for example `binary`, `text`, or `ping` frames.

Normal sockets only transfer bytes of data as payload, without any indication or information on what they signify - that is dependent on the upper layer protocols
that use these transport protocols.

### Contextual information

WebSockets carry in their connection establishment additional metadata, such as HTTP headers, an endpoint address and a list of supported subprotocols,
from which the server may select one it supports.

Normal sockets only differentiate connections by a tuple consisting of source and destination addresses and ports, with the destination port number
being the primary discriminator between services.

## Dynamic proxying

To allow `websocksy` to connect any protocol to a WebSocket endpoint despite these differences, there are two avenues of extensibility.

### Peer discovery backends

Peer discovery backends map the metadata from a WebSocket connection, such as HTTP headers (e.g. Cookies), the connection endpoint and any indicated
subprotocols to a peer address naming a 'normal' socket.

Backends can be loaded from shared libraries and may use any facilities available to them in order to find a peer - for example they may query a database
or [scan a file](plugins/backend_file.md) based on the supplied metadata. This allows the creation of dynamically configured bridge connections.

Currently, the following peer address schemes are supported:

* `tcp://<host>[:<port>]` - TCP client
* `udp://<host>[:<port>]` - UDP client
* `unix://<file>` - Unix socket, stream mode
* `unix-dgram://<file>` - Unix socket, datagram mode

The default backend integrated into `websocksy` returns the same (configurable) peer for any connection.

### Peer stream framing

To solve the problem of framing the data stream from TCP peers and selecting the correct WebSocket frame type, `websocksy` uses "framing functions",
which can be loaded as plugins from shared objects. These are called when data was received from the network peer to decide if and how many bytes are
to be framed and sent to the WebSocket, and with what frame type.

The framing function to be used for a connection is returned dynamically by the peer discovery backend. The backend may select from a library of different
framing functions (both built in and loaded from plugins).

`websocksy` comes with the following framing functions built in:

* `auto`: Send all data immediately, with the `text` type if the content was detected as valid UTF-8 string, otherwise use a `binary` frame
* `binary`: Send all data immediately as a `binary` frame
* `separator`: Waits until a variable-length separator is found in the stream and sends data up to and including that position as binary frame.
	Takes as parameter the separator string. The escape sequences `\r`, `\t`, `\n`, `\0`, `\f` and `\\` are recognized, arbitrary bytes may be specified
	hexadecimally using `\x<hex>`
* `newline`: Waits until a newline sequence is found in the stream and sends data up to and including that position as text frame if all data is valid
	UTF-8 (binary frame otherwise). The configuration string may be one of `crlf`, `lfcr`, `lf`, `cr` specifying the sequence to look for.

# Configuration / Usage

`websocksy` may either be configured by passing command line arguments or by specifying a configuration file to be read.

The listen port may either be exposed to incoming WebSocket clients directly or via a proxying webserver such as nginx.

Exactly one backend can be active at run time. Specifying the backend parameter a second time will unload the first backend
and load the requested one, losing all configuration. Options will be applied in the order specified to the backend loaded
at the time.

### Command line arguments

* `-p <port>`: Set the listen port for incoming WebSocket connections (Default: `8001`)
* `-l <host>`: Set the host for listening for incoming WebSocket connections (Default: `::`)
* `-k <seconds>`: Set the inactivity timeout for sending WebSocket keep-alive pings (Default: `30`)
* `-b <backend>`: Select external backend
* `-c <option>=<value>`: Pass configuration option to backend

### Configuration file

If only one option is passed to `websocksy`, it is interpreted as the path to a configuration file to be read.
A configuration file should consist of one `[core]` section, configuring central options and an optional `[backend]`
section, configuring the current backend. Options are set as lines of `<option> = <value>` pairs.

In the `[core]` section, the following options are recognized:

* `port`: Listen port for incoming WebSocket connections
* `listen`: Host for incoming WebSocket connections
* `ping`: Inactivity timeout for WebSocket keep-alive pings in seconds
* `backend`: External backend selection

In the `[backend]` section, all options are passed directly to the backend and thus are dependent on the specific
implementation. Backends should provide their own documentation files.

An [example configuration file](plugins/backend_file.cfg) using the `file` backend is available in the repository.

## Default backend

The integrated default backend takes the following configuration arguments:

* `host`: The peer address to connect to
* `port`: An explicit port specification for the peer (may be inferred from the host if not specified or ignored if not required)
* `framing`: The name of a framing function to be used (`auto` is used when none is specified)
* `protocol`: The subprotocol to negotiate with the WebSocket peer. If not set, only the empty protocol set is accepted, which
	fails clients indicating an explicitly supported subprotocol. The special value `*` matches the first available protocol. 
* `framing-config`: Configuration data for the framing function

## Plugins

This repository comes with some plugins already included, in addition to the built-in backend and framing functions.
Documentation for these resides in the `plugins/` directory.

* [backend\_file](plugins/backend_file.md) - Read connection peers from dynamic file paths
* [framing\_fixedlength](plugins/framing_fixedlength.md) - Frame the peer stream using fixed length segments
* [framing\_dynamic32](plugins/framing_dynamic32.md) - Frame the peer stream using 32bit length fields from the stream itself
* [framing\_json](plugins/framing_json.md) - Segment the peer stream into JSON entities

# Building

To build `websocksy`, you need the following things

* A working C compiler
* `make`
* `gnutls` and `libnettle` development packages (`nettle-dev` and `libgnutls28-dev` for Debian, respectively)

Run `make` in the project directory to build the core binary as well as the default plugins.

# Development

`websocksy` provides two major extension interfaces, which can be attached to by providing custom shared objects.
The core is written in C and designed to use as few external dependencies as possible.

All types and structures are defined in [`websocksy.h`](websocksy.h), along with their in-depth documentation.

The current API version is `1`. It is available via the compile-time define `WEBSOCKSY_API_VERSION`.

## Peer discovery backend API

When requested to load a backend, `websocksy` tries to load `backend_<name>.so` from the plugin path, which is a
compile time variable (overridable by providing the environment variable `PLUGINPATH` to `make`), which by default
points to the `plugins/` directory.

From the backend shared object, `websocksy` tries to resolve the following symbols:

* `init` (`uint64t init()`): Initialize the backend for operation. Called directly after loading the shared object.
	Must return the `WEBSOCKSY_API_VERSION` the module was built with.
* `configure` (`uint64_t configure(char* key, char* value)`): Set backend-specific configuration options.
	Backends should ship their own documentation on which configuration options they provide.
* `query` (`ws_peer_info query(char* endpoint, size_t protocols, char** protocol, size_t headers, ws_http_header* header, websocket* ws)`):
	The core function of a backend. Called once for each incoming WebSocket connection to provide a remote peer
	to be bridged. The fields in the returned structure should be allocated using `calloc` or `malloc` and
	will be `free`'d by the core.
* `cleanup` (`void cleanup()`): Release all allocated memory. Called in preparation to core shutdown.

Backends should take precautions and offer configuration for WebSocket requests that do not indicate a subprotocol
(the `protocols` argument to the backend `query` function will be `0`).
Backends should also provide a method for indicating that any subprotocol indicated is acceptable (and thus the first
one indicated, if any, will be selected). Idiomatically, this is done by accepting `*` as special value for the protocol
field.

The [`file` backend](plugins/backend_file.c) is provided as a reference backend implementation.

## Peer stream framing API

At startup, `websocksy` tries to load all shared objects in the plugin path (`plugins/` by default) that have a file
name ending in `.so` and not starting with `backend_` (which is reserved for backend plugins, of which only one may be
loaded at run time).

Plugins should declare their own initializer functions to be called at load time using e.g. the compiler's
`__attribute__((constructor))` syntax or the equivalent linker flag.

To register a framing function with the framing function library, the initializer function must use the 
`core_register_framing(char* name, ws_framing func)` API exported by the core.

The framing function is called once for every successful read from the peer it is used on, and again when it indicates
a frame boundary which leaves data in the buffer. It must return the number of bytes to be framed and sent to the
WebSocket client (Returning `0` indicates that the message is not complete and additional data is required from the
peer). To select the frame type, set the `opcode` argument. The default frame type is `binary` (`ws_frame_binary`).

The `framing_data` pointer can be used to store data on a per-connection basis.
If the pointer is nonzero when the connection is terminated, the function will be called with a NULL `data` pointer
as an indication that any allocation within `framing_data` is to be freed.

An example plugin providing the [`fixedlength` framing function](plugins/framing_fixedlength.c) is provided in the repository.
Additions to the `websocksy` plugin library are welcome!
