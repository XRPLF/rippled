# `src/libxrpl/json/Output.cpp`

## Role in the System

This file is the bridge between the XRPL codebase's in-memory JSON representation (`Json::Value`) and the streaming `Writer` serialization engine. It answers the question "given a fully-built `Json::Value` tree, how do I write it to an arbitrary output sink efficiently?" — without requiring callers to understand the `Writer` API directly.

The file is intentionally small: it contains exactly one non-trivial function and one convenience wrapper. Its purpose is separation of concerns rather than algorithmic complexity.

## The `Output` Type and Streaming Design

`Output` (defined in `Output.h`) is a type alias for `std::function<void(boost::beast::string_view const&)>`. It is a sink callback that receives arbitrarily-sized string fragments as they are produced. This design means serialized JSON is never buffered in a single contiguous allocation — fragments are pushed to the consumer as they are generated.

This matters in the XRPL context because ledger objects and transaction results can be large, deeply nested JSON structures. Streaming to a network buffer or an HTTP response body without building a full intermediate string reduces peak memory pressure on a server handling many concurrent RPC clients.

## Internal Structure

The file contains three functions, two of which share the name `outputJson`:

The private, anonymous-namespace overload `outputJson(Json::Value const&, Writer&)` is the core recursive engine. It dispatches on `value.type()` across all eight `Json::ValueType` enum cases — `nullValue`, `intValue`, `uintValue`, `realValue`, `stringValue`, `booleanValue`, `arrayValue`, and `objectValue` — calling the appropriate typed `writer.output()` overload for scalar values. For `arrayValue` and `objectValue` it calls `writer.startRoot()` to open a collection, iterates the children, and calls `writer.finish()` to close it.

For arrays, each element is preceded by `writer.rawAppend()` before the recursive call. This low-level method tells the `Writer` to emit a comma separator if the element is not first, without yet committing any value. The value is then written by the recursive `outputJson` call. For objects, `writer.rawSet(tag)` emits the quoted key and colon, and the recursive call emits the value. This two-step pattern (raw prefix + value) allows the recursive call to itself open sub-collections correctly — the `Writer`'s internal stack of open collections handles nesting transparently.

The public `outputJson(Json::Value const&, Output const&)` constructs a `Writer` on the stack (bound to the provided `Output` sink), then delegates to the private overload. The `Writer` destructor calls `finishAll()`, which closes any remaining open collections — this ensures a well-formed JSON document even in exceptional paths, though in practice the recursive walk will always call `finish()` explicitly for every collection it opens.

`jsonAsString(Json::Value const&)` is a convenience entry point. It allocates a `std::string`, wraps it with `stringOutput()` (an inline lambda in `Output.h` that appends each fragment via `s.append()`), and calls the same internal path. The `Output.h` header explicitly notes this function requires a full-size allocation and recommends `outputJson()` with a direct sink when memory is a concern.

## Design Decisions

The decision to keep the recursive worker in an anonymous namespace, while exposing only the `Output`-taking overload publicly, is a clean API contract: callers never need to instantiate a `Writer` themselves when serializing a pre-built `Json::Value`. Callers who want fine-grained streaming control (building JSON incrementally without a pre-built tree) use `Writer` directly.

No error handling exists in the switch statement, and none is needed. `Json::ValueType` is a closed enumeration; `Json::Value::type()` always returns one of the eight defined values. Adding a default case would be defensive noise rather than meaningful protection.

The `Writer` itself is O(1)-space (it maintains a fixed-depth stack of open collection states internally) and emits output in O(1)-granular chunks. The recursive nature of `outputJson` mirrors the recursive depth of the `Json::Value` tree, so the call stack grows with nesting depth — but this matches the natural shape of the data and is bounded by practical JSON structure limits in the XRPL protocol.