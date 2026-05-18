# `include/xrpl/json/to_string.h` — JSON Serialization Interface

This header provides the public serialization surface for `Json::Value` objects: three free functions that cover the two most common output needs (compact wire-format strings and human-readable strings) plus standard stream integration.

## Purpose and Placement

Within the `xrpl/json` module, the actual JSON machinery lives in `json_value.h`, `json_writer.h`, and their implementations. `to_string.h` is a deliberately thin façade over that machinery. It forward-declares `class Value` rather than including `json_value.h`, so any translation unit that only needs to convert a `Value` to a string or write it to a stream can include this header without pulling in the full JSON value and writer infrastructure. The include cost is just `<ostream>` and `<string>`.

## The Three Entry Points

`to_string(Value const&)` produces a compact, single-line JSON string. Its implementation delegates directly to `FastWriter().write(value)` — the `FastWriter` omits all whitespace and newlines, making it appropriate for RPC wire formats where byte count matters. This is the function to reach for whenever a `Json::Value` must be serialized for network transmission or storage.

`pretty(Value const&)` produces indented, human-readable JSON via `StyledWriter().write(value)`. The `StyledWriter` applies a 3-space indent per level, observes a 74-character right margin for deciding whether arrays expand to multiple lines, and generally produces output suitable for logging or debugging. When arrays are short and contain no objects or nested arrays, the styled writer keeps them on one line; otherwise it expands each element to its own line. This heuristic is baked into `StyledWriter::isMultilineArray()`.

`operator<<(std::ostream&, Value const& root)` is the stream insertion operator. Per the comment in `json_writer.h`, it uses `StyledStreamWriter` — meaning undecorated streaming of a `Json::Value` produces pretty-printed output, not compact output. This is the opposite of what many callers might expect from the default. Code that needs compact output to a stream should use the `Json::Compact` decorator from `json_writer.h` (e.g., `out << Json::Compact{std::move(jv)}`) or the `Json::stream()` template, both of which bypass `StyledStreamWriter` and write minimally.

## Design Observations

The split between `to_string.h` and `json_writer.h` reflects a deliberate layering decision. `json_writer.h` is the complete writer interface — it defines `FastWriter`, `StyledWriter`, `StyledStreamWriter`, the `Compact` decorator, and the `stream()` template. It also re-declares `operator<<`. `to_string.h` re-exposes only the three most commonly used operations under names that are idiomatic in the XRPL codebase (`to_string`, `pretty`, `operator<<`), hiding the underlying writer class selection from callers. This means callers who only serialize to `std::string` or a stream never need to know whether `FastWriter` or `StyledWriter` is the right choice — the function name carries that intent.

One subtle consequence: because `operator<<` is declared in both headers, any translation unit that includes only `to_string.h` will still find the operator, but the definition lives in the `json_writer` implementation. This works because both declarations refer to the same symbol, but it does mean `to_string.h` is implicitly relying on the linker to find the definition provided by `json_writer.cpp`.

The absence of any exception specification or error-return contract is intentional: JSON serialization of a well-formed `Value` is infallible. The writers operate on a pre-validated in-memory tree, so there is no meaningful error path to expose at this level.