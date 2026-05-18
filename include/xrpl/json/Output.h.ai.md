# `include/xrpl/json/Output.h`

This header defines the foundational output abstraction that the XRPL JSON streaming subsystem is built on. It is small by design: three declarations and a single inline factory, providing just enough interface to decouple JSON serialization from any specific output destination.

## The `Output` Type Alias

The central construct is a single type alias:

```cpp
using Output = std::function<void(boost::beast::string_view const&)>;
```

`Output` is a type-erased callable that accepts a read-only string chunk and does something with it — appends it to a buffer, writes it to a socket, sends it down an HTTP response stream, or discards it during testing. Using `std::function` here means any lambda, functor, or function pointer that matches the signature qualifies. Using `boost::beast::string_view` rather than `std::string_view` keeps this compatible with Boost.Beast's network I/O primitives without requiring copies.

The design deliberately separates *what to serialize* from *where to send the bytes*. `outputJson` and `Writer` (defined in `Writer.h`) know nothing about the transport — they call the `Output` object with consecutive string chunks as they traverse the JSON tree. This makes the serializer itself reusable across HTTP responses, WebSocket frames, log output, and in-memory buffers.

## `stringOutput()` — the Common-Case Sink

```cpp
inline Output
stringOutput(std::string& s)
{
    return [&](boost::beast::string_view const& b) { s.append(b.data(), b.size()); };
}
```

This factory captures a `std::string` by reference and returns an `Output` that accumulates all chunks into it. Because it takes the string by reference and the lambda captures by reference, the caller must keep the string alive for the duration of serialization — this is an intentional lifetime contract. The function is `inline` because it is trivially small and called frequently enough that the factory overhead should disappear after inlining.

## `outputJson()` and `jsonAsString()`

`outputJson(Json::Value const&, Output const&)` is the streaming path. In `Output.cpp` it constructs a `Writer` around the provided sink and then recursively walks the `Json::Value` tree — dispatching over scalar types via `Writer::output()` overloads and managing structural punctuation (commas, braces, brackets) via `Writer::rawAppend()` and `Writer::rawSet()`. Critically, no intermediate string buffer is built: bytes flow directly from the value tree to the `Output` function. For large JSON payloads (e.g., full ledger dumps or transaction history) this avoids a potentially multi-megabyte allocation that would otherwise live alongside the already-resident `Json::Value`.

`jsonAsString(Json::Value const&)` is the convenience wrapper for callers that need a self-contained `std::string`. It internally calls `stringOutput` to create a sink, drives the same `outputJson` path, and returns the accumulated string. The comments in the header are candid about the tradeoff: this form requires an allocation sized to the full output and should be avoided when the streaming variant is applicable.

Both functions produce *minimal* JSON — no indentation, no extra whitespace — which is appropriate for wire-format output where byte count matters. For human-readable formatting, the older `to_string.h`/`pretty()` path using `StyledWriter` is the alternative.

## Relationship to `Writer.h`

`Output.h` sits one level below `Writer.h` in the dependency graph. `Writer` takes an `Output` in its constructor and owns all the structural logic (stack-based collection tracking, comma insertion, key quoting). `Output.h` defines the sink contract; `Writer.h` defines the producer. This separation means a caller can construct a `Writer` without `Output.h` being visible — but the type still flows through cleanly because `Output` is a plain `std::function` with no further dependencies.