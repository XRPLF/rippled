# `include/xrpl/basics/ToString.h`

## Purpose

`ToString.h` provides a small family of `xrpl::to_string()` overloads that patch over two well-known deficiencies in the standard `std::to_string()` and make string conversion uniform across all primitive types in XRPL code.

## Why This File Exists

`std::to_string` has two inconvenient behaviors that appear repeatedly in ledger code:

- `std::to_string(true)` returns `"1"`, not `"true"`. Log messages, JSON serialization, and error strings throughout the rippled codebase need human-readable booleans.
- `std::to_string('A')` returns `"65"` — the integer value of the character — because `char` is an arithmetic type and the standard function treats it numerically.

Beyond these fixes, there is no single standard function that accepts `std::string` or `const char*` as identity cases. Generic template code that wants to uniformly convert any value to a string would have to branch on type. `xrpl::to_string` eliminates that branching.

## Design

The header defines five overloads under the `xrpl` namespace:

**Arithmetic template** (line 14–19): A SFINAE-constrained function template gated on `std::is_arithmetic<T>` that forwards to `std::to_string(t)`. This handles `int`, `long`, `float`, `double`, and the rest of the arithmetic family without any additional boilerplate. The `enable_if` guard prevents this template from competing with the non-template overloads below.

**`bool` overload** (line 21–25): Returns `"true"` or `"false"`. Even though `bool` satisfies `std::is_arithmetic`, C++ overload resolution prefers a non-template exact-match over a template instantiation. This overload therefore silently wins whenever a `bool` is passed, suppressing the `"0"`/`"1"` behavior.

**`char` overload** (line 27–31): Returns a one-character `std::string`. The same overload-resolution rule applies: `char` is arithmetic, but the non-template overload is preferred, preventing the integer-value conversion.

**`std::string` overload** (line 33–37): Identity conversion — returns the string by value. This is what makes generic code like `template<class T> std::string format(T v) { return xrpl::to_string(v); }` work for string arguments without special-casing them.

**`const char*` overload** (line 39–43): Constructs a `std::string` from a C-string literal, completing the string-identity family.

## Usage in the Codebase

The most visible consumer is `src/libxrpl/json/Writer.cpp`, which calls `xrpl::to_string(f)` when serializing `float` and `double` values to JSON output. The header is also `#include`d by `include/xrpl/json/Writer.h` and the XRPL transaction path debug-info header, and it appears indirectly wherever logging or JSON serialization needs to convert primitives to strings in a uniform way.

The comment in the header (`"It's also possible to provide implementation of to_string for a class which needs a string implementation"`) signals the intended extension point: XRPL domain types such as `Number` define their own `xrpl::to_string` overload in their own translation unit, and ADL (argument-dependent lookup) finds it automatically. `Number.cpp` demonstrates this with a full `xrpl::to_string(Number const&)` implementation that handles XRP and IOU formatting.

## Key Tradeoff

The header deliberately keeps the arithmetic template *enabled* for `bool` and `char` types — it is the non-template overloads that win in practice, not any exclusion in the template constraint. An alternative design would exclude `bool` and `char` from the `enable_if` (`!std::is_same<T,bool>` etc.), which would make the intent more explicit at the cost of a more complex constraint expression. The current approach is shorter and relies on well-defined overload-resolution rules, which is idiomatic modern C++.