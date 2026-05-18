# `src/libxrpl/json/Writer.cpp`

## Role and Purpose

`Writer.cpp` implements a streaming JSON serializer for the XRPL C++ codebase. Its defining property, stated in the header comment, is *O(1)-space, O(1)-granular output*: it never builds an intermediate JSON tree, and each write emits only a small, bounded chunk of bytes. This makes it the right tool for serializing large XRPL protocol messages, ledger state dumps, or RPC responses without materializing the entire JSON string in memory first.

The alternative — building a `Json::Value` tree and then calling `jsonAsString()` — is available in the same module but allocates memory proportional to the full output size. `Writer` exists precisely to avoid that allocation on the hot path.

## Architecture: Pimpl and the Output Sink

The public `Writer` class holds only a `std::unique_ptr<Impl>`. All state and logic live in `Writer::Impl`, defined entirely inside the `.cpp` file. This is a deliberate Pimpl pattern: callers depend on the public header without exposure to the `std::stack`, `std::set`, or `std::map` used internally.

The output destination is `Output`, which is `std::function<void(boost::beast::string_view const&)>` (declared in `Output.h`). This indirection means the writer can stream bytes to a string buffer, a network socket, a file, or any custom sink — the writer itself has no opinion on where the bytes land. `Output.h` provides `stringOutput(std::string&)` as a convenience factory for the common case.

## Collection Stack

The core of `Impl` is `Stack stack_`, a `std::stack<Collection, std::vector<Collection>>`. Each `Collection` entry represents one open JSON array or object:

- `type` distinguishes `array` from `object`.
- `isFirst` tracks whether a comma separator must precede the next entry. This is the standard trick for comma-separated lists: emit a comma before every entry except the first, rather than trying to suppress the trailing comma.
- `tags` (only present in `#ifndef NDEBUG` builds) is a `std::set<std::string>` that accumulates all object keys seen at that nesting level, catching duplicate keys at runtime before they produce semantically invalid JSON.

`start()` pushes a new collection and emits the opening `{` or `[`. `finish()` pops it and emits the matching `}` or `]`. `nextCollectionEntry()` validates that the stack is not empty and that the expected collection type matches the actual one, then either skips (for the first entry) or emits a comma.

## State Machine

`isStarted_` is a boolean that flips to `true` on the first call to any output method. `isFinished()` is `isStarted_ && stack_.empty()` — meaning: *we started writing and have closed all open collections*. The `markStarted()` function, called by every output path, asserts via `check()` that `isFinished()` is false. Attempting to write after the root collection closes throws a `std::logic_error`. This enforces a write-once contract: a `Writer` produces exactly one complete JSON value.

## RAII Completion Guarantee

`~Writer()` calls `impl_->finishAll()`, which closes any still-open arrays and objects. This makes the writer exception-safe: if a caller starts serializing a complex structure and throws mid-way, the destructor ensures the output stream still ends with a syntactically valid (though possibly semantically incomplete) JSON document. The header comment explicitly calls this out as useful for coroutines and exception-based control flow.

`Writer` is move-only — copy construction and assignment are deleted. Moving transfers `impl_` ownership and leaves the source with a null `impl_`; the destructor and `finishAll()` both guard with `if (impl_)`.

## String Escaping

`stringOutput()` in `Impl` handles the eight JSON special characters defined in `jsonSpecialCharacterEscape` (a `std::map<char, char const*>` at file scope). The implementation walks the input byte by byte but emits *runs* of clean characters in a single `output_()` call, only breaking the run when a special character is encountered. This means a typical ASCII string with no special characters is emitted in just three calls: the opening `"`, the entire string body, and the closing `"`.

## Float Serialization

`lengthWithoutTrailingZeros()` trims trailing zeros from floating-point string representations produced by `xrpl::to_string()`. The constant `integralFloatsBecomeInts` is hard-coded `false`, meaning `3.0` is serialized as `"3.0"` rather than `"3"`. The comment and constant name suggest this was a deliberate choice that was discussed and resolved — leaving the dead flag in place preserves that decision history without complicating the code.

## Public API Split: Templates vs. Primitives

The public header exposes `append<Scalar>()` and `set<>(tag, value)` as templates that call `rawAppend()`/`rawSet()` for structural plumbing (comma, colon, tag output) and then dispatch to one of the overloaded `output()` methods for the value. The `raw*` variants are exposed as an escape hatch for callers that handle value emission themselves — for instance, when writing a pre-formatted JSON fragment inline.

The template `output(Type t)` in the header falls back to `std::to_string(t)` via `implOutput()`, handling integer types and anything else that converts to string. Specializations for `bool`, `float`, `double`, `std::nullptr_t`, `std::string`, `char const*`, `Json::Value`, and `Json::StaticString` are provided as non-template overloads, ensuring the right formatting (e.g., `"true"` not `"1"` for booleans, `"null"` not `"0"` for null pointers).