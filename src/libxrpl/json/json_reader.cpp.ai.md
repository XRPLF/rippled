# `json_reader.cpp` — JSON Recursive-Descent Parser

## Role in the System

This file implements the `Json::Reader` class, the sole JSON deserialization component in `libxrpl`. Every inbound JSON document — RPC requests, configuration files, test fixtures — passes through this parser before any application logic sees it. It converts a raw text buffer into a `Json::Value` tree, the in-memory representation of structured data throughout the ledger codebase. Because `Reader` sits at the trust boundary for external input, it enforces several constraints stricter than the JSON specification requires.

## Architecture: Lexer + Recursive Descent

The implementation follows a classic two-phase design. `readToken()` acts as the lexer: it advances the `current_` pointer through the source buffer and classifies each syntactic unit into a `TokenType` enum (brace, bracket, string, integer, double, boolean, null, comment, etc.). The recursive-descent parser — `readValue()`, `readObject()`, and `readArray()` — then drives the lexer by calling `readToken()` and dispatching on the returned type.

The two phases are not cleanly separated in time: decoding (converting token bytes to typed values) happens lazily, only when a token is consumed. `decodeNumber()`, `decodeDouble()`, `decodeString()`, and `decodeUnicodeCodePoint()` operate on a `Token`'s `start_`/`end_` pointers into the original source buffer. This avoids allocating intermediate strings for tokens that are immediately validated and discarded.

## `nodes_` Stack and Value Population

The parser writes into the caller-supplied `Value& root` through a `std::stack<Value*>` named `nodes_`. On entry, the root pointer is pushed. Each time `readObject()` or `readArray()` descends into a child value, it takes a reference from the parent container (`currentValue()[name]` or `currentValue()[index]`) and pushes that child's address. After the recursive `readValue()` call returns, the child pointer is popped.

This means the recursion depth and the stack depth are always in sync: `readValue()` is called recursively, but parent tracking is maintained through `nodes_` rather than function parameters. The hard `nest_limit` of 25 levels is enforced at the start of every `readValue()` call, preventing stack exhaustion from adversarially nested documents — critical for a network-facing service.

## Deliberate Strictness: Root Type and Duplicate Keys

Two places where `Reader` is more restrictive than the JSON specification are worth noting. First, `parse(char const*, char const*, Value&)` validates after parsing that the root value is either an object or an array. A bare string, number, boolean, or null at the document root triggers a parse error. This matches RFC 4627 semantics and reflects how XRPL message framing works: every valid protocol message is a JSON object.

Second, `readObject()` calls `currentValue().isMember(name)` before inserting each key-value pair and returns an error if the name already exists. Standard JSON allows duplicate keys (with implementation-defined semantics for which value wins). Rejecting duplicates here prevents ambiguity in transaction parsing, where a second `"Amount"` field could silently shadow the first.

## Number Decoding

`readNumber()` classifies a token as `tokenInteger` or `tokenDouble` by scanning for floating-point indicators (`.`, `e`, `E`, `+`, `-`). Integers are then decoded by `decodeNumber()` using a `std::int64_t` accumulator, guarded by a `static_assert` that this type is wider than `Value::maxUInt`. If the absolute magnitude exceeds `Value::maxUInt`, the token is rejected as out of range. For non-negative values that fit in `Value::maxInt`, the result is stored as a signed `Value::Int`; larger values use `Value::UInt`. This preference for signed representation when possible reduces surprises in downstream comparisons.

`decodeDouble()` uses `sscanf` rather than `std::stod`. A comment in the source traces this choice to an OS X crash involving string-constant format arguments — the workaround is storing the format string `"%lf"` in a `char[]` array rather than passing a literal. A buffer of 32 characters handles the common case without heap allocation; longer strings fall back to a temporary `std::string`.

## String Decoding and Unicode

`decodeString()` processes the raw token bytes character by character, handling all JSON escape sequences (`\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`). The `\uXXXX` form is handled by `decodeUnicodeEscapeSequence()`, which manually converts four hex digits to a code point. `decodeUnicodeCodePoint()` wraps this and detects UTF-16 surrogate pairs: if the decoded value falls in the high-surrogate range U+D800–U+DBFF, a second `\uXXXX` sequence must immediately follow, and the two are combined using the standard formula `0x10000 + ((high & 0x3FF) << 10) + (low & 0x3FF)`. The resulting scalar is encoded to UTF-8 by `codePointToUTF8()`, which covers all four byte-length cases (up to U+10FFFF) following the RFC 3629 bit-layout directly.

## Error Reporting and Recovery

Parse errors are accumulated in a `std::deque<ErrorInfo>` rather than terminating on the first fault. Each `ErrorInfo` records the bad `Token` (start and end pointers into the source), a human-readable message, and an optional secondary `Location` for context (used when the error site differs from the relevant position). `addError()` always returns `false`, enabling the idiomatic pattern `return addError("message", token)` throughout the call chain.

`recoverFromError()` is invoked when a structural error (missing colon, missing closing brace, invalid value) is detected inside an object or array. It skips tokens until the expected terminator (`}` or `]`) or end of stream, discarding any secondary errors produced during the skip. This lets the parser continue and report multiple errors from a single malformed document.

`getFormattedErrorMessages()` converts accumulated errors to human-readable text with one-based line and column numbers. It walks the source from the beginning to compute line/column positions on demand — O(n) per error, but acceptable given that errors are exceptional.

## Comment Tolerance

The lexer recognizes both C-style (`/* ... */`) and C++-style (`// ...`) comments, assigning them `tokenComment`. `skipCommentTokens()` loops until a non-comment token is seen, making comments transparent to the rest of the parser. This is a deliberate extension beyond strict JSON — XRPL configuration and some internal tooling embed comments in JSON files.

## Public API and the `operator>>` Overload

The public surface is three `parse()` overloads: one taking `std::string`, one taking raw `char const*` pointers, and one taking `std::istream`. The `istream` variant slurps the entire stream into a `std::string` and delegates to the string overload rather than streaming tokens incrementally — a tradeoff that simplifies the implementation at the cost of buffering the whole document. A fourth templated overload in the header accepts Boost.Asio buffer sequences, assembling them into a `std::string` before parsing.

The free `operator>>(std::istream&, Value&)` provides stream extraction syntax. Unlike the member `parse()` methods that return `false` on failure, `operator>>` throws `std::runtime_error` via `xrpl::Throw<>`, making it suitable for code paths where failure is truly exceptional and propagation via return value would be burdensome.