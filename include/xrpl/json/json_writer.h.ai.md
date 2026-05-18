# `include/xrpl/json/json_writer.h` — JSON Serialization Strategies

This header is the central serialization layer for the `Json::Value` type used throughout rippled. It provides four distinct mechanisms for converting in-memory JSON trees to text, each suited to a different production context: a legacy class hierarchy for string output, a stream-friendly variant, a low-level template sink for zero-copy I/O, and a lightweight stream decorator. Understanding why four mechanisms exist — rather than one — requires reading how each is consumed.

## The Class Hierarchy: `WriterBase`, `FastWriter`, `StyledWriter`

`WriterBase` is a pure-virtual interface with a single `write(Value const& root) → std::string` method. This is the JsonCpp-era design: a polymorphic writer that produces an owned string. Two concrete implementations refine it:

`FastWriter` is the compact, single-line serializer. It accumulates all output into `document_` (a private `std::string`) via a recursive `writeValue()` helper, then returns the entire accumulated string. There is no indentation, no line breaks — the result is maximally dense. This is appropriate for machine consumption where byte count matters.

`StyledWriter` applies human-readable formatting with a configurable indent size (default 3 spaces) and right margin (74 columns). Its `isMultilineArray()` method embodies the key formatting heuristic: arrays of scalars that fit on a single line are inlined; arrays containing nested objects or other arrays, or arrays that exceed the right margin, are split across lines. This logic is implemented by staging child values into the `childValues_` vector before deciding layout, which is why the class carries that intermediate buffer as member state.

## `StyledStreamWriter`: Intentionally Outside the Hierarchy

`StyledStreamWriter` applies the same formatting rules as `StyledWriter` but writes to a `std::ostream*` instead of accumulating a string. The design note in the header is explicit: "There is no point in deriving from Writer, since `write()` should not return a value." This is not an oversight — forcing a stream-sink writer to conform to a string-returning interface would either require buffering the entire output (defeating the purpose) or returning a sentinel. The class deliberately opts out of `WriterBase` inheritance. Its constructor accepts a custom indentation string (defaulting to `"\t"`), unlike `StyledWriter`'s fixed `indentSize_` integer, giving callers more control over the whitespace character.

The `operator<<(std::ostream&, Value const&)` free function declared at the bottom of the public API surface delegates to `StyledStreamWriter`, making `std::cout << jv` work naturally at the cost of pretty-printing overhead.

## Template-Based Compact Serialization: `detail::write_value` and `stream()`

The more architecturally interesting addition is the `detail::write_value<Write>` template. It takes any callable with signature compatible with `void(void const*, std::size_t)` — a raw buffer sink — and dispatches on `Value::type()` with a `switch`. This avoids virtual dispatch entirely and allows the caller to wire in any sink: a socket buffer, an HTTP response body, a `boost::asio::streambuf`. The six `ValueType` cases (`nullValue`, `intValue`, `uintValue`, `realValue`, `stringValue`, `booleanValue`) are handled inline; `arrayValue` and `objectValue` recurse into child values.

The public-facing `stream(jv, write)` template wraps `detail::write_value` and appends a trailing `"\n"`, producing a complete newline-terminated compact JSON document. This is the mechanism used by the WebSocket and HTTP RPC server handlers in `WSInfoSub.h` and `ServerHandler.cpp`, where the write callback populates a low-level I/O buffer directly — no intermediate `std::string` allocation is needed.

## `Json::Compact`: A Value-Semantic Decorator

`Compact` is a small RAII wrapper that adapts a `Json::Value` for `operator<<` with compact (non-styled) formatting. Its constructor accepts only `Json::Value&&` (rvalue), which the header comment explicitly justifies: lvalue support would require a potentially expensive copy, so the interface forces callers to be explicit about ownership transfer or to pass a temporary. The `friend operator<<` delegates to `detail::write_value` with a lambda that forwards writes to the stream's `write()` method.

The usage pattern seen throughout the codebase confirms the intended role: `Json::Compact` appears ubiquitously in `JLOG` logging calls (consensus, dispute tracking, validation tries, performance logging) where pretty-printing would add unnecessary whitespace to log files, and where the JSON value is often a temporary produced by a `getJson()` call that would be discarded anyway. The rvalue-only constructor makes this pattern natural.

## Helper Functions

`valueToString()` is overloaded for `Int`, `UInt`, `double`, and `bool`. The integer variants use a stack-allocated 32-byte buffer with a manual right-to-left digit fill (via `uintToString()`), avoiding heap allocation and `std::to_string` overhead. The double variant applies 17-digit precision to guarantee round-trip fidelity. `valueToQuotedString()` handles JSON string escaping including control characters, and its implementation scans for control characters first to decide whether the slow escape path is needed — a small but real optimization for the common case of ASCII-clean strings.

## Relationship to the Broader JSON Module

Within the `include/xrpl/json/` module, `json_writer.h` is the serialization counterpart to `json_reader.h`. The `Output.h` file defines an alternative compact serializer (`outputJson` / `jsonAsString`) that uses a `std::function<void(string_view)>` sink rather than a template, trading compile-time flexibility for a stable ABI. The `Writer.h` streaming writer (`Json::Writer`) targets yet another use case — incremental, forward-only construction with O(1) memory — and is complementary rather than redundant.