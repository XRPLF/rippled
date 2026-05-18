# `include/xrpl/json/json_reader.h` — JSON Parser Interface

## Role in the System

`json_reader.h` defines `Json::Reader`, the single entry point for deserializing UTF-8 JSON text into the ledger's `Json::Value` tree. Every place in `rippled` that consumes external JSON — incoming RPC requests, peer-to-peer handshake bodies, validator-list manifests, ledger files loaded from disk, and CLI command arguments — routes through this class. It is a hand-rolled recursive-descent parser derived from the JsonCpp library, intentionally kept as a self-contained header plus one implementation file rather than pulling in a third-party JSON dependency.

## Public Interface

Four overloads of `parse()` cover every calling pattern present in the codebase:

- `parse(std::string const&, Value&)` — the most common form, used directly by `ServerHandler`, `RPCCall`, and `Application`.
- `parse(char const* beginDoc, char const* endDoc, Value&)` — the lowest-level overload; all other overloads eventually delegate here. Accepting a half-open pointer range avoids any extra copy when the caller already owns a buffer.
- `parse(std::istream&, Value&)` — reads the entire stream to a `std::string` first, then delegates to the string overload. The comment in the implementation acknowledges this is not streaming: a true iterator approach would need `parse` to be a template, which was intentionally avoided for simplicity.
- `parse(Value& root, BufferSequence const& bs)` — the template overload defined inline in the header. It accepts any Boost.Asio `ConstBufferSequence`, assembles the scattered buffers into a single `std::string` (using `buffer_size` to pre-reserve), and then calls the string overload. This is the form used in `ServerHandler` to parse raw HTTP request bodies directly off the network without an intermediate allocation step for reassembly.

All overloads return `bool`: `true` on success, `false` if any parse error was encountered. Error details are held inside the `Reader` instance and retrieved afterward via `getFormattedErrorMessages()`. This design deliberately separates parse result from diagnostics — call sites that care about error text (such as the RPC server returning a helpful message to the client) extract it separately; call sites that just need a go/no-go check can ignore it.

The companion `operator>>(std::istream&, Value&)` is a convenience wrapper that throws `std::exception` on failure, useful where exception-based error handling is preferred over checking a return value.

## Depth Limiting as a Security Invariant

`nest_limit` is declared `static constexpr unsigned` with the value `25`. The `readValue` method receives a `depth` counter and returns an error immediately if `depth > nest_limit`, before any further recursive calls. This cap prevents a crafted payload — an arbitrarily nested `[[[[...]]]]` array or object — from exhausting the call stack. In a network-facing server context this is a DoS mitigation: without it, a single malformed request could overflow the stack. The value 25 is generous for any legitimate ledger payload while being far below typical stack depths.

## Parser State and Token Design

The parser maintains its scanning state as five raw `char const*` fields: `begin_`, `end_`, `current_`, `lastValueEnd_`, and `lastValue_`. This is a zero-copy scan: the parser works directly against the original string data, never producing substrings until a value actually needs to be decoded (e.g., a string token decoded from `\uXXXX` escape sequences). The private `Token` class is correspondingly thin — a `TokenType` enum tag plus two `Location` (i.e., `char const*`) pointers bounding the lexeme in the source buffer. No heap allocation occurs per token.

The value tree is assembled using a `Nodes` stack (`std::stack<Value*>`). The root `Value` is pushed at parse start; as objects and arrays open and close, the matching `Value` node is pushed and popped. `currentValue()` returns a reference to the top-of-stack node, allowing `readValue` to write directly into the correct position in the tree without needing to pass the destination down through every call.

## Error Handling and Recovery

Errors are collected in an `Errors` deque (`std::deque<ErrorInfo>`), not thrown. Each `ErrorInfo` bundles a copy of the offending `Token`, a human-readable message string, and an optional secondary `Location` for context. `recoverFromError` and `addErrorAndRecover` implement best-effort continuation: after an unexpected token the parser attempts to skip ahead to a known safe point (e.g., the end of the current array or object), allowing it to accumulate multiple independent errors in one pass. `getFormattedErrorMessages()` formats the full deque into a single diagnostic string including line and column numbers — visible to callers like the RPC server that need to surface parse failures to clients.

## Relationship to Sibling Files

`json_forwards.h` provides the forward declaration of `Json::Value` that keeps this header's include footprint minimal. `json_value.h` defines the `Value` type that `Reader` populates; it carries all the `ValueType` variants (`nullValue`, `intValue`, `realValue`, `stringValue`, `booleanValue`, `arrayValue`, `objectValue`) that the parser maps its token types onto. `json_writer.h` provides the inverse direction — serializing a `Value` back to text. There is no shared state between `Reader` and `Writer`; they operate on `Value` objects independently.