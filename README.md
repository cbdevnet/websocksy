# websocksy

`websocksy` is a highly configurable [WebSocket (RFC6455)](https://tools.ietf.org/html/rfc6455) to 'normal' networking transport (TCP/UDP) bridge.
It is written in C and supports pluggable modules for bridge peer selection modules (for dynamic bridging) and stream framing.

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

### Peer discovery backend

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

# Configuration / Usage

`websocksy` may either be configured by passing command line arguments or by specifying a configuration file to be read.

The listen port may either be exposed to incoming WebSocket clients directly or via a proxying webserver such as nginx.

### Command line arguments

* `-p <port>`: Set the listen port for incoming WebSocket connections (Default: `8001`)
* `-l <host>`: Set the host for listening for incoming WebSocket connections (Default: `::`)
* `-b <backend>`: Select external backend
* `-c <option>=<value>`: Pass configuration option to backend

### Configuration file

If only one option is passed to `websocksy`, it is interpreted as the path to a configuration file to be read.
A configuration file should consist of one `[core]` section, configuring central options and an optional `[backend]`
section, configuring the current backend. Options are set as lines of `<option> = <value>` pairs.

In the `[core]` section, the following options are recognized:

* `port`: Listen port for incoming WebSocket connections
* `listen`: Host for incoming WebSocket connections
* `backend`: External backend selection

In the `[backend]` section, all options are passed directly to the backend and thus are dependent on the specific
implementation. Backends should provide their own documentation files.

An [example configuration file](websocksy.cfg) using the `file` backend is available in the repository.

## Default backend

The integrated default backend takes the following configuration arguments:

* `host`: The peer address to connect to
* `port`: An explicit port specification for the peer (may be inferred from the host if not specified or ignored if not required)
* `framing`: The name of a framing function to be used (`auto` is used when none is specified)
* `protocol`: The subprotocol to negotiate with the WebSocket peer. If not set, only the empty protocol set is accepted, which fails clients indicating
	an explicitly supported subprotocol. The special value `*` matches the first available protocol. 
* `framing-config`: Arguments to the framing function

# Building

To build `websocksy`, you need the following things

* A working C compiler
* `make`
* `gnutls` and `libnettle` development packages (`libnettle-dev` and `libgnutls28-dev` for Debian, respectively)

Run `make` in the project directory to build the core binary as well as the default plugins.

# Development

TBD
