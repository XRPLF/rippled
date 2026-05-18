# `json_body.h` — Custom Boost.Beast HTTP Body Type for JSON

`json_body.h` defines a custom HTTP body type for use with Boost.Beast's typed HTTP message framework. It serves as the glue layer between XRPL's `Json::Value` representation and the byte-level serialization machinery that Beast uses when building and sending HTTP responses. This file is consumed in two parts of the rippled codebase: `OverlayImpl.cpp` (for the overlay's diagnostic HTTP endpoints — `/crawl`, `/health`, `/vl/`) and `ServerHandler.cpp` (for the RPC dispatch layer).

## Role in Beast's Body Concept

Boost.Beast's HTTP library is parameterized on body type: `http::message<isRequest, Body, Fields>`. Any type used as `Body` must declare `value_type` (the in-memory representation), and optionally provide nested `reader` and `writer` types that handle serialization. In `json_body`, `value_type` is `Json::Value`, and both `reader` and `writer` implement the Beast BodyReader concept — that is, both serialize a `Json::Value` body into wire bytes. The comments in both `get()` methods explicitly call out the BodyReader requirement, including the non-obvious detail that `get()` must return `boost::optional` rather than `std::optional` to satisfy Beast's older concept interface.

The typical usage pattern in the overlay layer is:
```cpp
boost::beast::http::response<json_body> msg;
msg.body()["key"] = Json::Value(...);
msg.prepare_payload();
return std::make_shared<SimpleWriter>(msg);
```

`SimpleWriter` then serializes the entire message (headers + body) using `boost::beast::ostream`, which internally invokes Beast's serializer and therefore calls into `json_body::reader`.

## The `reader` Class (Streaming Serialization)

`reader` implements the older-style Beast BodyReader interface, where the constructor takes the entire `http::message<isRequest, json_body, Fields>` rather than a separate header and body value. All work happens eagerly at construction time: the constructor calls `Json::stream(m.body, ...)`, which is a template function in `xrpl/json/json_writer.h` that walks the `Json::Value` tree and emits compact JSON text plus a trailing newline through a callback. That callback copies each chunk into a `boost::beast::multi_buffer` held as a member of the `reader` instance.

The `init()` method is intentionally a no-op marked `noexcept` — because the buffer is already fully populated by the time `init()` is called, there is nothing to do. `get()` returns the buffer contents paired with `false`, signaling to Beast that all data is available in a single call with no more to follow. `finish()` is also a no-op.

The choice of `multi_buffer` here means the serialized output can span multiple discontiguous memory regions, which is fine since `multi_buffer::const_buffers_type` (a sequence of `const_buffer`) satisfies Beast's requirements and can be passed directly to async I/O operations.

## The `writer` Class (One-Shot Serialization)

`writer` implements the newer-style Beast BodyReader interface, where the constructor takes `(http::header<isRequest, Fields> const&, value_type const&)` — the header and body value as separate arguments. It uses `Json::to_string()` rather than `Json::stream()`, eagerly serializing the entire `Json::Value` into an `std::string` member. `get()` wraps that string as a single `boost::asio::const_buffer`, again with `false` to indicate completion in one shot. The `init()` method explicitly clears the `error_code` to signal success (contrasting with `reader::init()`, which simply does nothing).

The two classes differ primarily in their Beast API style (old vs. new constructor form) and in their serialization function (`Json::stream()` using chunked writing callbacks vs. `to_string()` producing a full string in one allocation). Both approaches are fully eager — neither defers or streams incrementally.

## Design Notes

The struct deliberately has no data members of its own; it exists only to host the type aliases and nested classes. The `explicit json_body() = default` declaration prevents accidental implicit construction without changing any behavior, since `json_body` instances are never created at runtime — only its nested types are instantiated by Beast's internal machinery.

The absence of a BodyWriter (Beast's deserialization concept) is intentional: in every call site, `json_body` is used exclusively for outbound responses. There is no case where the codebase needs to parse an incoming HTTP request into a `Json::Value` using this body type; that path goes through `http_request_type` (which uses `boost::beast::http::dynamic_body`) followed by manual `Json::Reader` parsing.

The `is_deferred = std::false_type` member in `reader` informs Beast that the reader does not need to defer buffer preparation. Combined with the upfront-construction design, this keeps the serialization path simple and synchronous: by the time the async I/O layer asks for bytes, they are already in the buffer.