# The `json` framing function

The `json` framing function segments the peer stream at boundaries defined by complete JSON
entities (Objects, Arrays, Strings or Values) and forwards these as text frames.

For example, on encountering the start of a JSON object at the beginning of the stream, it will read from the
peer until that objects closing brace and the send the entire object as a single text frame.

The plugin does not implement a full parser and performs only rudimentary syntactic analysis to determine
the end of the current entity. When encountering an error, the entire buffer is forwarded as a binary frame.
It is possible to construct erroneous JSON objects that pass the parser without an error, however any
syntactically correct object should be forwarded correctly (please file a bug with an example otherwise).

## Configuration

The `json` framing function does not require any configuration.
