# `include/xrpl/beast/utility/rngfill.h`

## Role and Purpose

`rngfill.h` provides two thin template functions for filling a contiguous region of memory with random bytes drawn from any C++ *UniformRandomNumberEngine*-compliant generator. Its goal is to bridge the conceptual gap between a typed random-number generator — which produces values of some fixed integer width — and raw byte buffers, which are the natural unit of cryptographic key material and binary test data.

The file lives in the `beast` utility layer, a low-level support library used throughout the XRPL codebase. It is included by cryptographic primitives (`Seed.cpp`, `SecretKey.cpp`), networking infrastructure (`BaseWSPeer.h`), and test helpers (`TestBase.h`), making it one of the most broadly depended-upon utility headers in the project.

## The Raw-Buffer Overload

```cpp
template <class Generator>
void rngfill(void* const buffer, std::size_t const bytes, Generator& g);
```

This overload handles the general case where the buffer size may be any positive number of bytes. Because a standard generator produces values of `result_type` (e.g., 8 bytes for a `uint64_t`-based engine), the function divides the fill into two phases:

1. **Complete iterations** — as many full `result_type`-sized chunks as fit in `bytes`, each copied via `std::memcpy`.
2. **Remainder** — if the buffer size is not a multiple of `sizeof(result_type)`, a final generator call provides one more value, of which only the first `bytes_remaining` bytes are copied.

Using `std::memcpy` is the deliberate and correct choice here. Casting a `uint8_t*` buffer to `result_type*` and writing through it would violate strict-aliasing rules. The `memcpy` approach produces identical code after optimization while keeping the C++ type system satisfied.

This overload is used in `randomSeed()` (filling a 16-byte array) and `randomSecretKey()` (filling a 32-byte buffer), both of which call into `crypto_prng()` — the XRPL cryptographically secure PRNG backed by OpenSSL, whose `result_type` is `uint64_t`. Sixteen bytes divides evenly by 8, so no remainder path executes there, but the general overload handles both cases correctly regardless.

## The Typed Array Overload

```cpp
template <class Generator, std::size_t N,
    class = std::enable_if_t<N % sizeof(typename Generator::result_type) == 0>>
void rngfill(std::array<std::uint8_t, N>& a, Generator& g);
```

This overload is restricted to `std::array<uint8_t, N>` where `N` is known at compile time and is statically required — via `std::enable_if_t` — to be an exact multiple of `sizeof(Generator::result_type)`. The compile-time divisibility constraint eliminates the partial-fill branch entirely, allowing a tighter loop that writes directly through a `result_type*` pointer cast over the array's storage:

```cpp
result_type* p = reinterpret_cast<result_type*>(a.data());
while (i--) *p++ = g();
```

This is safe because `std::array<uint8_t, N>` provides `data()` which returns properly aligned storage, and the `enable_if` constraint ensures no out-of-bounds write is possible. The trade-off is a narrower call signature — only fixed-size byte arrays where the size divides cleanly into generator words qualify — in exchange for a simpler, branchless implementation.

## Design Philosophy

Both overloads are generator-agnostic by design. The same `rngfill` works with `crypto_prng()` for security-sensitive operations and with `xor_shift_engine` for high-throughput test-data generation (as seen in `TestBase.h`). This polymorphism via the *UniformRandomNumberEngine* concept keeps call sites clean and allows the fill strategy to be selected by substituting the generator, not by selecting a different fill function.

The absence of any return value or error path reflects the assumption that a well-formed generator never fails. Any failure condition (e.g., entropy exhaustion in the CSPRNG) is the generator's responsibility to handle, not the fill utility's.

The `#include <xrpl/beast/utility/instrumentation.h>` header is pulled in as a precautionary dependency — `instrumentation.h` defines assertion macros (`XRPL_ASSERT`, `UNREACHABLE`, etc.) used throughout the beast utility layer — though `rngfill.h` itself contains no assertions.