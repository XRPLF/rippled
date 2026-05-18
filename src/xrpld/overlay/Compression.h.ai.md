# `Compression.h` — Overlay Compression Facade

This header provides the compression abstraction layer used by the XRPL peer-to-peer overlay when serializing and transmitting protocol messages. It sits between the high-level `Message` class (which decides *whether* to compress a message) and the low-level `CompressionAlgorithms.h` (which implements the actual codec), serving as a thin dispatch layer that centralizes algorithm selection, exception suppression, and the wire-format constants that define the XRPL binary message protocol.

## Wire Format Constants and Their Significance

The two size constants — `headerBytes = 6` and `headerBytesCompressed = 10` — are not arbitrary; they define the two legal XRPL message header formats on the wire. An uncompressed message header occupies exactly 6 bytes: 4 bytes encoding the payload length (with the top 6 bits reserved/zeroed) followed by 2 bytes for the protobuf message type. A compressed message header occupies 10 bytes: the same first 4 bytes now carry the compression algorithm in the top 4 bits, followed by 2 bytes for the message type, followed by 4 additional bytes encoding the original uncompressed size. That extra 4-byte field is what pushes the compressed header from 6 to 10 bytes and is the reason `Message::compress()` checks whether the compressed payload actually saves more than 4 bytes over the uncompressed form before committing to the compressed path.

These constants are consumed directly by `ProtocolMessage.h` for header parsing and by `Message.cpp` for buffer sizing, making them a shared specification across the send and receive paths.

## The `Algorithm` Enum Encoding

The `Algorithm` enum carries an important invariant documented with an inline comment: all values other than `None` must have the high bit set, and the low nibble must be zero. `LZ4 = 0x90` satisfies this — binary `1001 0000`. This encoding is deliberate: on the receive side, `ProtocolMessage::parseMessageHeader()` first checks `*iter & 0x80` to detect a compressed message, then extracts the algorithm via `*iter & 0xF0`, and validates that the reserved bits `*iter & 0x0C` are zero. The enum value can therefore be extracted directly by masking the first wire byte, with no further translation needed. Adding a new algorithm in the future would require choosing a value with the high bit set and a zero low nibble (e.g., `0xA0`, `0xB0`) to remain compatible with this decoding scheme.

`None = 0x00` deliberately uses a zero high bit, which is how the uncompressed path is signaled at the protocol level without any special-casing in the parser.

## `compress()` and `decompress()` — Exception Boundaries

Both functions are function templates that delegate immediately to `CompressionAlgorithms.h`, but they serve a distinct purpose: they are **exception-to-zero-return converters**. The underlying `lz4Compress` and `lz4Decompress` implementations throw `std::runtime_error` on failure, but the overlay's send and receive paths cannot propagate exceptions. These wrappers catch all exceptions and return `0`, which callers treat as a failure signal.

The asymmetry in template parameters reflects the asymmetry in network I/O. `decompress()` accepts a `ZeroCopyInputStream` (a protobuf abstraction for scatter-gather network buffers) because incoming data arrives in discontiguous chunks from Boost.Asio. The stream-based overload of `lz4Decompress` handles chunk stitching: it tries to use the first chunk directly if it is large enough to hold the entire compressed payload, and only falls back to a contiguous copy if the data spans multiple chunks. `compress()`, by contrast, operates on data that is already serialized into a contiguous `buffer_` in `Message`, so it takes a raw `void const*`.

The `BufferFactory` template parameter for `compress()` implements a lazy allocation pattern: rather than pre-allocating a fixed output buffer, the caller provides a callable that accepts the required capacity (computed from `LZ4_compressBound`) and returns a pointer to an appropriately sized buffer. In practice, `Message::compress()` passes a lambda that calls `bufferCompressed_.resize()` and returns a pointer offset by `headerBytesCompressed`, so the codec writes compressed bytes directly into the final wire buffer leaving the header region intact.

## Algorithm Extensibility Guard

Both dispatch functions contain an `else` branch that logs a warning and calls `UNREACHABLE`. These branches are marked `LCOV_EXCL_START`/`LCOV_EXCL_STOP` because they cannot be reached with the current algorithm set. The `UNREACHABLE` macro is a deliberate design signal: if a new `Algorithm` enum value is added without updating these dispatch functions, the sanitizer or assertion framework will catch it at runtime rather than silently falling through to a zero-return. This makes the enum an open-coded extensibility point with a mechanical safety net.

## Relationship to Sibling Files

`Compression.h` is included by `Message.h`, making it a transitive dependency for anything that constructs or inspects overlay messages. `ProtocolMessage.h` uses the `headerBytes`, `headerBytesCompressed`, and `Algorithm` symbols directly for parsing incoming streams. `CompressionAlgorithms.h` is the only non-overlay dependency and contains the actual LZ4 calls; its placement in `include/xrpl/basics/` (the shared protocol library) rather than `src/xrpld/overlay/` keeps the codec reusable outside the overlay while `Compression.h` provides the overlay-specific dispatch policy on top of it.