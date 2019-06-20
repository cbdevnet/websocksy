# The `fixedlength` framing function

The `fixedlength` peer stream framing function segments the peer stream into binary frames
at fixed byte boundaries (for example, aggregating and then sending 32-byte fragments from the
peer stream).

## Configuration

The framing configuration string is simply the length of the segments to transmit in bytes.
