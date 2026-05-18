# `SimpleWriter.h` — Synchronous HTTP Message Serialization for the `Writer` Interface

`SimpleWriter` is a concrete, synchronous implementation of the abstract `Writer` interface that serializes an entire Boost.Beast HTTP message into an in-memory buffer at construction time. It exists to satisfy the `Handoff::response` contract when the full HTTP response is known immediately — the common case for short error messages, redirect responses, and simple JSON replies throughout the XRPL server and overlay subsystems.

## The `Writer` Contract

The abstract base class `Writer` defines a pull-based streaming interface with four virtual methods: `complete()`, `consume()`, `prepare()`, and `data()`. This design allows the network session layer to consume response data incrementally — useful when generating large or asynchronous payloads where data arrives in chunks. The session calls `prepare()` to signal readiness, reads buffer segments via `data()`, and advances the stream with `consume()`.

`SimpleWriter` implements this interface for the degenerate but common case: the entire payload already exists in memory before the first byte is sent.

## Design: Eager Serialization at Construction

The constructor is a function template over `isRequest`, `Body`, and `Fields` — the three template parameters of `boost::beast::http::message`. This universality lets `SimpleWriter` wrap any valid Beast HTTP message (request or response, any body type, any field set) with a single class, without virtual dispatch or type erasure at the message layer. Serialization happens immediately via `boost::beast::ostream(sb_) << msg`, which writes the fully framed HTTP/1 wire representation into the internal `boost::beast::multi_buffer`. After the constructor returns, `SimpleWriter` holds no reference to the original message; ownership and lifetime are entirely self-contained.

This eager approach has a deliberate tradeoff: the entire message must fit in memory and be ready at construction. If the message body is large, the allocation happens upfront. The payoff is that `prepare()` becomes trivially synchronous — it always returns `true` immediately and ignores the `resume` callback entirely, because there is never anything asynchronous to wait for.

## Buffer Management via `multi_buffer`

The backing store is a `boost::beast::multi_buffer`, which is a Boost.Asio dynamic buffer composed of multiple non-contiguous fixed-size chunks. The `data()` method iterates over those chunks and copies their `const_buffer` descriptors into a `std::vector<boost::asio::const_buffer>`, producing a scatter-gather buffer sequence that the session layer can pass directly to `async_write`. The `consume()` method delegates to `multi_buffer::consume()`, which advances the read position through the chunk list without copying data. `complete()` checks `sb_.size() == 0` — the canonical way to detect that all bytes have been consumed.

## Usage in Practice

`SimpleWriter` is the go-to response writer across two major subsystems:

In `ServerHandler.cpp`, it wraps protocol-rejection responses (e.g., "Invalid protocol") and final HTTP replies to RPC clients. In `OverlayImpl.cpp`, it wraps peer redirect responses (`/crawl`, `/vl/<key>`, `/health`) and error rejections for bad peer handshakes. In both cases, the pattern is identical: build a fully populated Beast HTTP message, call `prepare_payload()` to compute content-length, then `std::make_shared<SimpleWriter>(msg)` and store it in `Handoff::response`. The session layer then drains the writer without needing to know anything about message construction.

## Deprecation

The class is annotated `/// Deprecated`. The most likely reason is that the eagerly-in-memory approach is incompatible with large or streaming response bodies, and that newer or planned writer implementations handle those cases properly. Despite the deprecation tag, it remains widely used throughout the codebase wherever responses are small and immediately available — its simplicity is a feature in those contexts. Callers that need async or streaming behavior would require a different `Writer` subclass.

## Error Handling

There is intentionally no error handling within the class body itself. If `boost::beast::ostream` throws during construction (e.g., due to allocation failure), the exception escapes and no object is created. Subsequent operations on a successfully constructed `SimpleWriter` are infallible: they operate only on pre-allocated buffer memory with no further I/O.