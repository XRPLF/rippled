# `ProtocolMessage.h` — Overlay Wire Protocol Deserialization

This header is the message-decoding spine of the XRPL overlay P2P network. It owns the entire pipeline from raw Boost.Asio buffer sequences — as received from a peer TCP connection — to typed protobuf message objects dispatched to a handler. No other layer touches the on-wire bytes for protocol messages; everything passes through here.

## Wire Format and `MessageHeader`

The XRPL overlay uses two distinct header layouts, distinguished by whether the high bit of the first byte is set.

**Uncompressed (6 bytes):** The top 6 bits of the first byte must be zero — they act as a format guard. The remaining 26 bits encode the payload size. The next 2 bytes hold the message type.

**Compressed (10 bytes):** The high bit is always 1, allowing `parseMessageHeader()` to branch on `*iter & 0x80`. The next 3 bits carry the compression algorithm in the top nibble of the first byte (`0x90` for LZ4; bits 2–3 must be zero and are validated). The 26-bit payload size follows, then 2 bytes of message type, then 4 bytes of uncompressed size. The extra 4 bytes explain the jump from 6 to 10 bytes — the receiver needs the uncompressed size to allocate the decompression target buffer.

`MessageHeader` is a plain struct that captures all decoded fields: `total_wire_size` (header + payload), `header_size` (6 or 10), `payload_wire_size` (compressed or raw payload), `uncompressed_size`, `message_type`, and the `compression::Algorithm` enum. Keeping these pre-computed avoids re-parsing during the content phase.

## Three-Stage Decoding Pipeline

### `parseMessageHeader()`

A buffer-sequence iterator walks the first bytes and populates a `MessageHeader`, returning `std::optional<MessageHeader>`. The error code distinguishes three states:

- `errc::success` with a null optional: not enough bytes yet — caller should wait for more data.
- `errc::no_message`: the header guard bits didn't match either format — malformed stream, drop the connection.
- `errc::protocol_error`: bits 2–3 of the first byte were set, or the algorithm identifier isn't LZ4 — invalid compression framing.

This three-way return avoids exception overhead on the hot path, which is important since this runs once per received message.

### `parseMessageContent<T>()`

A `std::enable_if` guard constrains `T` to subclasses of `google::protobuf::Message`, catching misuse at compile time. The function constructs a `ZeroCopyInputStream<Buffers>` adapter (defined in `ZeroCopyStream.h`) over the raw buffer sequence, then calls `Skip(header.header_size)` to skip past the header bytes without copying them.

For uncompressed messages, protobuf's `ParseFromZeroCopyStream` reads directly from the adapter, incurring zero extra copies even when the data spans multiple non-contiguous ASIO buffer segments.

For compressed messages, the path must allocate a contiguous `std::vector<uint8_t>` of `header.uncompressed_size` bytes, run `xrpl::compression::decompress()` (LZ4 via `ZeroCopyInputStream`), and then call `ParseFromArray`. The copy is unavoidable because LZ4 decompression requires a contiguous output region and protobuf parsing also needs one. The allocation is bounded by the 64 MiB `maximumMessageSize` check applied before this function is reached.

### `invoke<T>()`

Glues parsing to dispatch. It calls `parseMessageContent<T>`, then fires three callbacks on the handler: `onMessageBegin()` (receives wire size, uncompressed size, and a compression flag — used by `PeerImp` for traffic metrics), `onMessage()` (the typed processing callback), and `onMessageEnd()` (post-message hook). Separating begin/end from the message itself lets the handler bracket processing with timing or resource accounting.

## Public Entry Point: `invokeProtocolMessage()`

This is the single function called from `PeerImp`'s read loop. It:

1. Calls `parseMessageHeader()` and returns early if the header is incomplete or malformed.
2. Enforces a 64 MiB ceiling on both `payload_wire_size` and `uncompressed_size` via `maximumMessageSize` — a safeguard against memory exhaustion attacks.
3. Checks `handler.compressionEnabled()`. If the peer negotiated uncompressed-only but sent a compressed header, the connection is closed with `protocol_error`. This prevents a peer from forcing CPU work without negotiation.
4. If `total_wire_size > size`, sets `hint = total_wire_size - size` — the exact number of bytes still needed — and returns zero consumed bytes. The caller passes this hint back to the ASIO read operation to reduce system call overhead.
5. Dispatches on `header->message_type` via an exhaustive `switch`, instantiating `detail::invoke<TM…>()` for each known protobuf type. Unknown types call `handler.onMessageUnknown()` and return success, enabling forward-compatible protocol evolution without hard failures.

The function returns `{bytes_consumed, error_code}`. A zero `bytes_consumed` with no error means "incomplete — try again later"; a non-zero error means the peer should be disconnected.

## Handler Concept

`Handler` is an unconstrained template parameter, resolved duck-typing style. The concrete implementation is `PeerImp`, which provides `compressionEnabled()`, `onMessageBegin()`, `onMessage()` (via overload for each protobuf type), `onMessageEnd()`, and `onMessageUnknown()`. Using a template rather than a virtual base class lets the compiler inline all dispatch at the call site — important given that `invokeProtocolMessage` sits directly on the message-processing hot path.

## Supporting Utilities

`protocolMessageName()` maps integer type codes to human-readable strings for logging. The three `protocolMessageType()` overloads do the inverse for a small subset of types (`TMGetLedger`, `TMReplayDeltaRequest`, `TMProofPathRequest`), used by outbound code that needs to construct a wire header from a message object rather than a numeric constant.

`buffersBegin()` and `buffersEnd()` are small helpers that instantiate `boost::asio::buffers_iterator` with a fixed `uint8_t` value type, preventing repeated verbose template instantiation throughout `parseMessageHeader`.