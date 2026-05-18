# `xor_shift_engine.h` — XOR-Shift 128+ PRNG

## Role in the System

This header defines `beast::xor_shift_engine`, the XRPL ledger's default pseudo-random number generator. It is a self-contained, header-only implementation of the xorshift128+ algorithm, exposed as `beast::xor_shift_engine` — a type alias consumed directly by `xrpl::default_prng()` in `include/xrpl/basics/random.h`. Every non-cryptographic random number produced across the node — test data generation, shuffle operations, timing jitter, and buffer fills via `beast::rngfill` — ultimately flows through this engine.

## Algorithm: xorshift128+

The `operator()` body is a verbatim implementation of xorshift128+ as published at [xorshift.di.unimi.it](http://xorshift.di.unimi.it/xorshift128plus.c):

```cpp
result_type s1 = s_[0];
result_type const s0 = s_[1];
s_[0] = s0;
s1 ^= s1 << 23;
return (s_[1] = (s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))) + s0;
```

The state is two 64-bit words (`s_[0]`, `s_[1]`), giving a 128-bit combined state and a period of 2¹²⁸ − 1. The algorithm is deliberately built on unsigned integer arithmetic whose overflow wraps mod 2⁶⁴; this is why the file appears in the UBSAN suppression list (`ubsan.supp`) under both `unsigned-integer-overflow` and `undefined` categories — the overflow is load-bearing and intentional, not a bug.

## Seeding and the MurmurHash3 Finalizer

A direct consequence of xorshift is that a zero state will produce nothing but zeros forever. The `seed()` method therefore throws `std::domain_error` on a zero input, making the invariant explicit rather than silently producing a degenerate sequence.

Beyond the zero-exclusion, the seed value is not used raw. Both state words are initialised through the MurmurHash3 finalizer (`murmurhash3`):

```cpp
s_[0] = murmurhash3(seed);
s_[1] = murmurhash3(s_[0]);
```

MurmurHash3's finalizer (the two multiply-xor-shift avalanche rounds with constants `0xff51afd7ed558ccd` and `0xc4ceb9fe1a85ec53`) is a well-known technique for eliminating seed clustering. Without it, nearby seed values produce nearly identical initial states because xorshift is linear; the finalizer mixes the bits so that seeds differing by one bit produce uncorrelated initial states. The two sequential applications also ensure `s_[0] ≠ s_[1]` for any nonzero seed (since `murmurhash3` is a bijection), preventing an illegal all-zero state through the back door.

## C++ Engine Interface

The class satisfies the C++ `UniformRandomBitGenerator` named requirement: it provides `result_type`, `min()`, `max()`, and `operator()()`. The `min()` and `max()` return `std::numeric_limits<uint64_t>::min/max()`, covering the full 64-bit range. This makes it directly usable with every `std::uniform_*_distribution` adapter, as demonstrated in `random.h`'s `rand_int` family and in `rngfill.h`'s raw-buffer fill loop, which chunks calls to `operator()()` into 8-byte writes.

`random.h` also asserts these properties at compile time:

```cpp
static_assert(std::is_integral<beast::xor_shift_engine::result_type>::value && ...);
static_assert(std::numeric_limits<beast::xor_shift_engine::result_type>::max() >= ...uint64_t::max());
```

This ensures that the type alias can never be swapped for a narrower engine without a build failure.

## Dummy Template Parameter

The class is declared as `template <class = void> class xor_shift_engine` inside `namespace detail`, then exposed as a type alias `using xor_shift_engine = detail::xor_shift_engine<>`. This is a common C++ header-only pattern: the dummy template parameter makes the class a class template, so its member function definitions in the same header are treated as implicit instantiations rather than external definitions, avoiding One Definition Rule violations when the header is included from multiple translation units. There is no intention of specialising the template; the `class _` parameter is permanently unused.

## Threading Model

The engine itself carries no locks or thread-safety guarantees — it is a plain value type with two `uint64_t` words of mutable state. Thread safety is the caller's responsibility. `default_prng()` in `random.h` addresses this with `thread_local` storage: each thread gets its own `xor_shift_engine` instance, seeded non-deterministically from `std::random_device` through a shared seeder that is itself an `xor_shift_engine` protected by a `std::mutex`. Concurrent reads from different threads are therefore isolated without any contention at the call site.

## What It Is Not

The comment in `random.h` is unambiguous: this engine is **not cryptographically secure**. Keys, IVs, secure cookies, and any material that must be unpredictable to an adversary must use a different source of randomness. `xor_shift_engine` is purely for performance-sensitive internal operations where statistical quality — not secrecy — is the requirement.