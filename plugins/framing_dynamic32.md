# The `dynamic32` framing function

The `dynamic32` peer stream framing function segments the peer stream into binary frames
at dynamic byte boundaries inferred from the stream data itself. The function will read
a 32bit length value at a specified offset from the stream and treat it as the length
of the next segment to be framed.

## Configuration

The framing configuration string may be a comma-separated list of options of the form
`<key>=<value>`, containing one or more of the following keys:

* `offset`: Set the offset (in bytes) of the 32-bit segment size in the stream, counted from the start of a segment/stream (Default: `0`)
* `static`: Static amount to add to the segment length, e.g. to account for fixed headers (Default: `0`)
* `endian`: Set the byte order of the transmitteed size to either `big` or `little` (Default: `little`)

The total size of the segment sent to the Websocket peer can be calculated using the formula

```
offset + 4 /*32bit size field*/ + static + size field content
```
