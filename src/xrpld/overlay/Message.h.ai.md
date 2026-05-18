# `xrpld/overlay/Message.h` — Wire Protocol Message Framing

`Message` is the serialization envelope for every protobuf message that travels between XRPL peers on the overlay network. It solves a specific problem that is easy to underestimate: a single broadcast message — a validation, a transaction, a ledger chunk — may be sent to dozens of peers simultaneously. This class encapsulates the serialized bytes once, compresses them at most once, and lets all concurrent sends share the same buffer safely.

## What a `Message` Contains

At construction, the protobuf object is serialized immediately into `buffer_`, prepended by a 6-byte wire header. The header format is documented in detail inside `setHeader()` in the `.cpp`:

- **Uncompressed (6 bytes):** 6 reserved zero bits, a 26-bit payload size, and a 16-bit message type. All multi-byte values are big-endian.
- **Compressed (10 bytes):** The first bit signals compression; the next 3 bits name the algorithm (currently only LZ4, encoded as `0x90`); 2 reserved bits; 26-bit *compressed* payload size; 16-bit message type; 32-bit original uncompressed size.

The distinction matters because the receiver needs to know how large a buffer to allocate for decompression before it can decode the protobuf. Both formats share the same first 6 bytes, with the compressed form appending 4 extra bytes for the original size field.

The hard limit `maximumMessageSize = megabytes(64)` is enforced by the receiving path — a 26-bit payload field physically cannot represent more than 64 MiB.

## Lazy, Once-Only Compression

The second buffer, `bufferCompressed_`, is populated lazily. `getBuffer(Compressed::On)` calls `std::call_once(once_flag_, &Message::compress, this)`, which means compression runs exactly once no matter how many threads call `getBuffer` concurrently. This is the central design trade-off: CPU for LZ4 compression is spent once and amortized across all N peer sends, rather than once per peer.

The `compress()` method applies a hard-coded compressibility policy before even attempting compression:

1. Messages smaller than 70 bytes are skipped entirely — LZ4 overhead would negate any benefit.
2. Only a specific whitelist of message types is eligible: `mtMANIFESTS`, `mtENDPOINTS`, `mtTRANSACTION`, `mtGET_LEDGER`, `mtLEDGER_DATA`, `mtGET_OBJECTS`, `mtVALIDATOR_LIST`, `mtVALIDATOR_LIST_COLLECTION`, `mtREPLAY_DELTA_RESPONSE`, and `mtTRANSACTIONS`. High-frequency small messages like `mtPING`, `mtVALIDATION`, `mtPROPOSE_LEDGER`, and `mtSTATUS_CHANGE` are deliberately excluded.
3. Even if LZ4 is attempted, if the compressed result is not smaller than the uncompressed payload minus the extra 4 header bytes (the net savings threshold), `bufferCompressed_` is cleared and the uncompressed buffer is returned by `getBuffer()` as a fallback.

This fallback is why `getBuffer()` checks `bufferCompressed_.empty()` after calling `compress()` rather than unconditionally returning the compressed buffer.

## Traffic Accounting

At construction time, `TrafficCount::categorize()` classifies the message into one of roughly 50 fine-grained traffic categories (`transaction`, `validation`, `ledger_data`, `get_hash_ledger`, etc.) and stores the result in `category_`. This is intentional: the category is computed once during serialization, before the message is queued. In `PeerImp::send()`, the category is read via `getCategory()` to report outbound traffic metrics without re-inspecting the protobuf type.

## Squelch Integration

The optional `validatorKey_` field is provided specifically for `mtVALIDATION` and `mtPROPOSE_LEDGER` messages. In `PeerImp::send()`, if a validator key is present, the squelch check `squelch_.expireSquelch(*validator)` gates whether the message is actually transmitted to that peer. When a peer is squelched for a given validator, the message bytes are counted under `TrafficCount::squelch_suppressed` and the send is skipped without ever calling `boost::asio::async_write`. Passing the key through `Message` rather than the call site keeps the broadcast path uniform — the same `shared_ptr<Message>` flows through all peer send queues, and each `PeerImp` independently decides whether to skip or transmit it.

## Lifetime and Async Safety

`Message` inherits `std::enable_shared_from_this<Message>`. The key invariant is that `getBuffer()` returns a `const&` to an internal `std::vector`. In `PeerImp`, the message is held in `send_queue_` (a `std::queue<shared_ptr<Message>>`), which keeps the object alive for the duration of the `boost::asio::async_write` call that passes `buffer_.data()` directly to the kernel. The message is dequeued only after `onWriteMessage` fires, ensuring the buffer lifetime outlives the async operation. Compression writes to `bufferCompressed_` are protected by `std::call_once`, so there is no race between the compression attempt and concurrent reads of the compressed buffer by other threads calling `getBuffer`.

## Relationship to `Compression.h`

`Compression.h` provides the `compress()` and `decompress()` template functions over LZ4 (via `CompressionAlgorithms.h`), plus the `Algorithm` and `Compressed` enums and the header size constants (`headerBytes = 6`, `headerBytesCompressed = 10`). `Message` owns the higher-level policy of *when* to compress; `Compression.h` supplies the mechanism. The `Compressed::On/Off` enum flows all the way from the peer-level negotiation (stored as `compressionEnabled_` in `PeerImp`) through `getBuffer()` to `compress()`.