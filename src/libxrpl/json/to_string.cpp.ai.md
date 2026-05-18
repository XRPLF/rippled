# `src/libxrpl/json/to_string.cpp`

This file provides the two canonical entry points for serializing a `Json::Value` to a `std::string` within the XRPL JSON library: `to_string` for compact output and `pretty` for human-readable output. Both functions live in the `Json` namespace and are declared in `include/xrpl/json/to_string.h`.

The implementation is intentionally minimal — each function simply constructs a writer object on the stack and immediately calls its `write` method:

- `to_string` delegates to `FastWriter`, which emits the entire JSON document on a single line with no extraneous whitespace. This is the workhorse serializer used wherever bandwidth or parse overhead matters, such as network RPC responses and log output.
- `pretty` delegates to `StyledWriter`, which applies indentation (3 spaces per level) and a right-margin heuristic (74 characters) to decide when arrays should break across lines. It is intended for diagnostic output and human inspection.

Both writer classes inherit from `WriterBase` and encapsulate their intermediate state (the accumulating `document_` string, indentation tracking, child value buffers) as private member data. Constructing them on the stack per call keeps each serialization operation stateless and thread-safe — there is no shared mutable state between calls.

A third, lower-level path also exists in `json_writer.h`: the `detail::write_value` template and the `stream()` / `Compact` helpers bypass the writer class hierarchy entirely, writing chunk-by-chunk to an arbitrary callable. That path is used when the caller owns the output sink (e.g., a `Beast` HTTP stream). `to_string` and `pretty` are the simpler, string-returning façade over the same underlying value-traversal logic.

No validation occurs in either function; all type-dispatch and escaping happen inside `FastWriter::writeValue` and `StyledWriter::writeValue` respectively. The `to_string.cpp` layer is purely a naming and convenience concern.