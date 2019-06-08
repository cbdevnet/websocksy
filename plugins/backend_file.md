# The `file` backend

The `file` peer discovery backend traverses one or several configurable files to find a peer for an incoming
WebSocket connection.

## Operation

For any incoming WebSocket connection, the backend constructs a series of file names to read from a set of
user-configurable templates which may use the following variables:

* `%endpoint%`: The complete endpoint path (minus trailing and leading slashes)
* `%cookie:<name>%`: If present, parse the HTTP `Cookie` header to find a cookie by name and replace the variable with the cookie's value
* `%header:<tag>%`: Find a header by its tag and replace the variable by it's value if present

All slashes in variable values are replaced by underscores to defend against directory traversal attacks.

If a specified file or variable does not exist (ie. the header or cookie is missing), the expression is skipped and the next
one is evaluated. When the end of the template expression list is reached without a valid peer being found, the WebSocket connection
will be rejected.

The files accessed via the expression list must contain lines of the form

`<network peer> <subprotocol> [<framing>] [<framing-config>]`

For example:
```
tcp://localhost:5900 binary auto
unix:///tmp/app.sock text text
```

If the WebSocket client indicates one or more supported subprotocols, the first line found in any of the traversed
files matching any of the indicated protocols is used as the connection peer. The special value `*` for the
protocol always matches the first indicated protocol. If the client does not indicate a subprotocol, the first
configuration line read will be used.

## Backend configuration

This backend accepts the following configuration options:

* `path`: The base path for the template expression list. All template evaluations will be appended to this path.
* `expression`: Adds a template expression to the list. Specify multiple times to extend the list. Expressions are evaluated in the order they are specified.

An [example configuration file](backend_file.cfg) is provided in the repository.
