# `src/libxrpl/json/json_writer.cpp`

## Role in the System

This file is the serialization engine for XRPL's embedded JSON library — a modified descendant of JsonCpp. It converts `Json::Value` trees into well-formed JSON text, providing three distinct output strategies suited to different runtime needs. It is the counterpart to `json_reader.cpp` (parsing) and works directly against the `Value` type defined in `json_value.cpp`.

The public surface is straightforward: `FastWriter` for compact single-line output, `StyledWriter` for human-readable indented strings, and `StyledStreamWriter` for human-readable output directly to a `std::ostream`. The companion file `to_string.cpp` wraps the two string-returning writers behind the convenience functions `Json::to_string()` and `Json::pretty()`.

## Primitive Serialization Helpers

The file opens with a small set of standalone conversion functions that form the serialization substrate for all three writers.

`uintToString()` writes digits in reverse into a stack-allocated 32-byte buffer by walking a pointer from the end toward the front, then returns that pointer as the start of the number string. This avoids heap allocation and string reversal, trading a slightly non-obvious pointer dance for efficiency. `XRPL_ASSERT(current >= buffer)` guards against the impossible case of integer overflow causing the pointer to escape the buffer — impossible in practice since 32 bytes is far larger than any 64-bit integer's decimal representation, but present as a safety net.

`valueToString(double)` uses `%.16g` format to preserve full double precision without forcing a trailing decimal point. The comment explicitly notes that JSON doesn't distinguish reals from integers in its grammar, so the `#` alternative format flag is unnecessary.

`valueToQuotedString()` is the most security-relevant helper. It takes a raw C-string and returns a properly JSON-escaped, double-quote-delimited string. The fast path uses `strpbrk` to scan for the common special characters and `containsControlCharacter()` for the U+0001–U+001F range in a single pass; if neither fires, the string is clean and the function simply wraps it in quotes. The slow path allocates a result string pre-reserved to `2 * strlen + 3` bytes (worst-case all characters needing escape, plus surrounding quotes and null), then walks character by character emitting the appropriate two-character JSON escape sequence. Control characters outside the named escapes emit Unicode escapes in the form `\uXXXX` via `std::ostringstream`. Notably, forward slashes are *not* escaped despite the comment acknowledging they could be for JavaScript `</` compatibility — a documented intentional choice.

## `FastWriter`: Compact Single-Line Output

`FastWriter::write()` resets the `document_` member string, delegates to the recursive `writeValue()`, and returns the result via `std::move`. The write logic is a straightforward `switch` on `Value::type()`, appending directly to `document_` with no whitespace, indentation, or newlines. Arrays emit `[elem,elem]` and objects emit `{"key":value,"key":value}` with no extra spacing. This is the format used by `Json::to_string()` and is appropriate for wire protocols and logging where bandwidth or readability are unimportant.

## `StyledWriter` and `StyledStreamWriter`: Formatted Output

These two classes are nearly mirror images of each other. `StyledWriter` accumulates into a `std::string document_` member, while `StyledStreamWriter` writes directly to a `std::ostream* document_` pointer. The stream variant sets `document_` to `nullptr` after `write()` completes — a small defensive measure to catch use-after-write bugs where someone holds a reference to the writer and mistakenly calls methods outside a `write()` session.

Both classes share the same formatting rules: objects always expand to multi-line with each member on its own indented line; arrays use an intelligent single-line-vs-multi-line heuristic.

### The `isMultilineArray` Heuristic and Dual-Mode `pushValue`

The most architecturally interesting mechanism in the file is how `StyledWriter` (and `StyledStreamWriter`) decide array layout. `isMultilineArray()` applies a two-stage check:

1. **Quick check**: if `size * 3 >= rightMargin_` (74 by default) — meaning three characters per element would already saturate the line — or if any element is a non-empty object or array, the array is immediately considered multi-line.

2. **Dry-run check**: if the quick check passes, the method needs to know the actual rendered widths of each element. It sets the `addChildValues_` flag to `true` and calls `writeValue()` on each element. When `addChildValues_` is set, `pushValue()` diverts output from `document_` into the `childValues_` vector instead of writing it. After the loop, if the computed total line length exceeds `rightMargin_`, the array becomes multi-line. The `childValues_` vector is then reused during the actual render pass.

This is a clever re-use of the normal write machinery to perform a speculative measurement run without any special measurement code. The tradeoff is that `writeValue()` can be called twice for each element of a potentially single-line array: once during measurement and once during final output. For the typical short arrays of ledger data this is negligible, but it is worth noting as a potential inefficiency for deeply nested or wide JSON structures.

The `writeIndent()` implementations differ in a subtle way: `StyledWriter`'s version checks whether the last character in `document_` is already a space or newline to avoid double-indenting, whereas `StyledStreamWriter::writeIndent()` simply unconditionally emits `'\n' + indentString_` — the commented-out block in the stream version acknowledges this simplification was intentional, trading correctness in the "last char was space" edge case for simplicity, which is acceptable since the stream variant is typically used for final human-readable output rather than intermediate composition.

`StyledStreamWriter` accepts a custom `indentation` string in its constructor (defaulting to `"\t"`), whereas `StyledWriter` hard-codes a fixed `indentSize_` of 3 spaces. This makes `StyledStreamWriter` more flexible for consumers who need configurable indentation.

## `operator<<` and the Header's `detail::write_value`

The free `operator<<(std::ostream&, Value const&)` overload at the bottom of the file makes any `Json::Value` streamable, delegating to `StyledStreamWriter` with default tab indentation. This is the format used when values appear in log output.

The header (`json_writer.h`) also defines a template `detail::write_value` and the `Json::Compact` wrapper class. These provide a third compact-output pathway that bypasses the class hierarchy entirely: `write_value` is a function template parameterized on a write callable with signature `void(void const*, std::size_t)`, enabling zero-overhead integration with websocket send buffers or other scatter-gather I/O without constructing intermediate strings. `Json::Compact` is a move-only wrapper that enables `out << Json::Compact{std::move(jv)}` syntax for inserting compact JSON into a stream without the default styled formatting — a neat design that avoids naming conflicts with `operator<<` for full `Value` objects while still reusing the stream insertion idiom.

## Design Notes

The three-class hierarchy balances competing concerns: `FastWriter` is stateful but reusable (reset on each `write()` call), `StyledWriter` similarly resets per call, and `StyledStreamWriter` avoids string accumulation entirely for large documents. The `WriterBase` abstract interface is defined in the header for `FastWriter` and `StyledWriter` but deliberately omitted for `StyledStreamWriter` — as the header comment explains, a stream writer's `write()` must accept an `ostream&` parameter and cannot meaningfully return `std::string`, so forcing it into the `WriterBase` hierarchy would require an awkward adapter. The XRPL codebase handles this by exposing the stream writer independently and providing the streaming `operator<<` as the primary integration point.