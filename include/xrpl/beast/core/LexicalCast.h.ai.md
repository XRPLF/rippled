# `beast/core/LexicalCast.h` — Lexical Type Conversion Utilities

This header provides the `beast` namespace's lexical casting facility: safe, allocation-minimal conversion between string representations and numeric or boolean C++ types. It exists because `boost::lexical_cast` carries significant overhead (locale handling, exceptions as control flow, heap allocation), while `std::stoi`/`std::stoul` and similar functions have locale sensitivity and inconsistent error signaling. `LexicalCast.h` offers a uniform interface backed by `std::from_chars` — locale-independent, non-allocating, and precise about error conditions.

## Template Specialization Architecture

The implementation uses the classic C++ partial-specialization pattern: a `detail::LexicalCast<Out, In>` functor struct with `operator()` that does the actual conversion. Because partial specialization of function templates is not allowed in C++, the work is split into struct specializations and then thin public free-function wrappers call into them. This makes it straightforward to add new source or destination types without touching existing code.

The specialization tree is:

- **`LexicalCast<std::string, In>`** — converts arithmetic types via `std::to_string`, and enums by first casting to their `underlying_type` before delegating. The enum path is important because XRPL uses typed enumerations in configuration contexts where human-readable integers are expected.

- **`LexicalCast<Out, std::string_view>`** — the canonical parsing specialization. A `static_assert` enforces that `Out` must be integral; floating-point conversion is intentionally unsupported. The implementation uses `std::from_chars` for zero-allocation, locale-independent parsing. One notable detail: `std::from_chars` rejects a leading `+` sign, so the specialization manually advances the pointer past `+` before calling it. This accommodates formats like `"+1"` that can appear in config or protocol fields.

  The `bool` overload within this specialization is handled separately (via SFINAE with `!std::is_same_v<Integral, bool>`): it lowercases the input and accepts `"1"`, `"true"`, `"0"`, or `"false"`, returning false for anything else. This prevents the surprising default behavior where any non-zero string would be truthy.

- **`LexicalCast<Out, boost::core::basic_string_view<char>>`** — a forwarding shim to the `std::string_view` specialization. The comment in the code explains its origin: as of early 2024, Boost uses `boost::core::basic_string_view<char>` internally for HTTP header values (used in Boost.Beast's HTTP layer), and `Handshake.cpp` passes these header values directly to `lexicalCastChecked`. Without this specialization, there would be no implicit conversion and the call would fail to compile.

- **`LexicalCast<Out, std::string>`** and the two `char*`/`char const*` variants — all forward to the `string_view` specialization. The pointer variants include `XRPL_ASSERT` checks for null, which in production builds map to `assert(...)` and in Antithesis fuzzing builds become structured assertions for the fuzzer's invariant engine.

## Public API

Three free functions expose the facility:

`lexicalCastChecked(out, in)` returns `bool` — false on any parse or range error. This is the primary building block. It places the result in an output parameter rather than returning it, consistent with the checked conversion idiom where success and value are separate concerns. Callers like `ProtocolVersion.cpp` use it in guarded patterns:

```cpp
if (!beast::lexicalCastChecked(major, std::string(m[1])))
    return std::nullopt;
```

`lexicalCastThrow<Out>(in)` returns `Out` by value and throws `BadLexicalCast` (a subclass of `std::bad_cast`) on failure. This is used when the caller considers parse failure a programming error or exceptional condition.

`lexicalCast<Out>(in, defaultValue)` returns the parsed value or `defaultValue` on failure. The default for `defaultValue` is `Out()` (zero-initialized), so `lexicalCast<uint16_t>(portStr)` returns `0` on failure — a property explicitly exploited in `StringUtilities.cpp`, where port `0` is then treated as an invalid URL:

```cpp
pUrl.port = beast::lexicalCast<std::uint16_t>(port);
if (pUrl.port == 0)
    return false;
```

## Error Handling Tradeoffs

The three-API design reflects the three legitimate calling styles: checked return for structured validation, exception for defensive contracts, and default-fallback for simple config parsing. Rather than unifying on exceptions (as `boost::lexical_cast` does) or solely on error codes, the header provides all three entry points atop a single shared functor. The `BadLexicalCast` exception class is minimal by design — it carries no message payload, because at the sites where it is thrown the context is already known to the caller.

## Scope Limitation: Integrals Only

The `static_assert` in `LexicalCast<Out, std::string_view>` deliberately restricts the output type to integrals. Floating-point parsing is excluded. XRPL's configuration and protocol layers do not round-trip floating-point values as strings in ways that require this facility, so supporting it would add complexity (locale handling, rounding modes) without a clear use case. Any attempt to instantiate `lexicalCast<double>(...)` fails at compile time with a clear message.