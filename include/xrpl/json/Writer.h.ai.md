# `include/xrpl/json/Writer.h` — Streaming JSON Writer

## Role in the System

`Json::Writer` exists to solve a specific performance problem in the XRPL node: when serializing large ledger objects or transaction sets to JSON for RPC responses, building an intermediate `Json::Value` tree first means allocating a heap structure proportional to the output size before any bytes reach the network. `Writer` eliminates this by emitting JSON directly into an `Output` callback — a `std::function<void(boost::beast::string_view const&)>` — as each field is written, never holding more than a fixed stack of `Collection` state objects. The class comment calls this O(1)-space and O(1)-granular, meaning both memory usage and per-step CPU work are bounded regardless of how large the JSON document becomes.

## Architecture: Pimpl with a Collection Stack

The public class is a thin shell over a `std::unique_ptr<Impl>`. The implementation lives entirely in `Writer.cpp`'s anonymous namespace and `Writer::Impl`. This lets `Writer` be move-constructible (the `unique_ptr` transfers ownership) without exposing internal types in the header.

`Impl` maintains a `std::stack<Collection>` (backed by a `std::vector` for contiguous memory). Each `Collection` tracks:

- `type` — whether the current scope is an `array` or an `object`
- `isFirst` — whether any item has been written yet, which controls whether a leading comma must be emitted before the next element
- `tags` — a `std::set<std::string>` present **only in debug builds** (`#ifndef NDEBUG`) that catches duplicate object keys at runtime

Every time a new array or object starts, a `Collection` is pushed. Every time `finish()` is called, the closing `]` or `}` is emitted and the top is popped. `finishAll()` drains the stack to empty, and the destructor calls `finishAll()` unconditionally — so partial writes caused by exceptions or coroutine cleanups always produce syntactically valid (if incomplete-content-wise) JSON.

## Key API Patterns

The API is split into three conceptual layers:

**Collection control** — `startRoot()`, `startAppend()`, `startSet()`, `finish()`, `finishAll()`. The naming encodes both what kind of collection is being opened and where it appears: `startRoot` for top level, `startAppend` when the collection becomes an element of an array, `startSet` when it becomes the value of an object key. There is no separate `startObject`/`startArray` family; the `CollectionType` enum parameter carries that distinction.

**Structured writes** — `append(Scalar)` and `set(tag, Value)` are templates that compose two lower-level calls: `rawAppend()` or `rawSet()` (which handle comma insertion and object-tag emission) followed by `output(t)` (which serializes the scalar value). This split enables the `raw*` variants for callers that want to write the value data themselves — useful when bridging with other serialization code.

**Scalar output** — A family of overloaded `output()` methods covers `std::string`, `char const*`, `Json::Value`, `nullptr_t`, `float`, `double`, `bool`, and a template fallback using `std::to_string()`. String values go through `Impl::stringOutput()`, which scans the bytes for the eight JSON-special characters (quote, backslash, slash, and control characters) and emits escape sequences in-place without ever constructing a fully escaped copy of the string. Floats and doubles use `xrpl::to_string()` then `lengthWithoutTrailingZeros()` to strip cosmetic trailing zeros while preserving at least one decimal digit.

## Invariant Enforcement and the `check()` Free Function

The header exports a free `check(bool, std::string)` function that throws `std::logic_error` via `xrpl::Throw`. This is used throughout `Impl` to guard against illegal call sequences: calling `append` outside an array, calling `set` outside an object, calling any write after the writer is "finished" (stack drained after at least one write). These are programming errors, not runtime data errors, so `logic_error` is appropriate. In release builds the duplicate-tag detection is compiled out, but the structural checks (`empty()`, `isFinished()`) remain active.

## Design Tradeoffs

The central tradeoff is forward-only writing. The caller must visit fields in output order; there is no way to go back and insert a key earlier in the stream. This rules out use cases that need to compute a field's value lazily after other fields are known. In practice within XRPL, JSON serialization follows a determined field order, so this is not a real constraint.

The existing `Json::Value`-based serialization path (`outputJson()` in `Output.h`) handles the reverse case — converting an already-built tree to a stream — and is used when `Writer::output(Json::Value const&)` is called, allowing arbitrary subtrees to be embedded mid-stream without requiring the caller to decompose them field by field.

The `isStarted_` flag in `Impl` distinguishes a fresh `Writer` (no writes yet) from a finished one (stack empty after writing). Without this, destroying a never-used `Writer` would incorrectly try to call `finishAll()` in a way that could misbehave. The destructor checks `impl_` for null before calling through, which protects against the moved-from state where `impl_` has been transferred out.