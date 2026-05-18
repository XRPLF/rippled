# `include/xrpl/crypto/csprng.h` — Cryptographically Secure PRNG Interface

## Role in the System

This header defines the engine that feeds every piece of key material in the XRP Ledger: random wallet seeds, secret keys, nonces, and session identifiers all flow through `csprng_engine`. It exists as a thin, type-safe C++ wrapper around OpenSSL's `RAND_bytes` so the rest of the codebase never has to touch OpenSSL directly for randomness and always gets thread safety and standard-library compatibility for free.

## `csprng_engine` Design

`csprng_engine` satisfies the C++ *UniformRandomNumberEngine* named requirement. This means it exposes `result_type`, `operator()()`, and the `constexpr` `min()`/`max()` pair. That conformance lets the engine be passed directly to standard-library facilities like `std::uniform_int_distribution` or `beast::rngfill`, which is exactly how callers such as `randomSeed()` and `randomSecretKey()` use it in `Seed.cpp` and `SecretKey.cpp`.

Copy and move construction and assignment are all explicitly deleted. The engine holds a `std::mutex`, is backed by a global OpenSSL state, and is exposed as a singleton — copying it would produce a second object with the same mutex but no coherent relationship to the underlying PRNG state. Deleting these operations makes that misuse impossible at compile time.

## Construction and Teardown

The constructor calls `RAND_poll()`, which on most platforms causes OpenSSL to harvest seed material from the OS (e.g., `/dev/urandom` on Linux, `CryptGenRandom` on Windows). The comment in the source notes this is "not strictly necessary" because OpenSSL auto-seeds lazily, but calling it eagerly surfaces seeding failures at startup rather than at first use. Any failure throws `std::runtime_error`.

The destructor conditionally calls `RAND_cleanup()` only for OpenSSL versions older than 1.1.0, where cleanup is needed to release internal state. On modern OpenSSL the call was removed because the library manages cleanup internally through `atexit`.

## Thread Safety

Thread safety is version-conditioned at compile time:

```cpp
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || !defined(OPENSSL_THREADS)
    std::lock_guard lock(mutex_);
#endif
```

OpenSSL 1.1.0 made `RAND_bytes` internally thread-safe when compiled with thread support. On a modern build the mutex acquisition in `operator()(void*, size_t)` is entirely elided — the only overhead is the `RAND_bytes` call itself. The mutex is still present in the class for `mix_entropy`, which always holds it around `RAND_add`. This split avoids serializing the hot path (generating bytes) while still protecting the less-frequent entropy mixing path, which modifies shared pool state.

## Entropy Mixing

`mix_entropy(void* buffer, std::size_t count)` serves two purposes: it is called during initialization to stir in extra OS randomness, and it can be called by application code that has obtained high-quality entropy from an external source (hardware RNG, user input, etc.).

The implementation allocates 128 values from `std::random_device` on the stack, then passes them to `RAND_add` with an entropy estimate of **zero**. This conservative choice is deliberate — on some platforms `std::random_device` may fall back to a PRNG, so claiming zero entropy ensures OpenSSL never raises its internal entropy-satisfied threshold based on potentially weak input. Real entropy is mixed in without degrading OpenSSL's accounting. The caller-supplied `buffer` is also added with a zero entropy claim for the same reason.

## `crypto_prng()` Singleton

```cpp
csprng_engine& crypto_prng();
```

The free function returns a reference to a `static csprng_engine` instance. Meyers-singleton initialization is guaranteed to be thread-safe by the C++11 standard, so the first call from any thread safely constructs the engine exactly once. Returning a reference (not a value) ensures every caller shares the same OpenSSL RNG pool and the same mutex, preserving the thread-safety guarantees. Callers should never attempt to copy or store the engine by value — the deleted copy/move operations prevent this.

## Failure Modes

If `RAND_bytes` returns anything other than 1, the engine throws `std::runtime_error("CSPRNG: Insufficient entropy")`. This is an unrecoverable condition: generating cryptographic material from an entropy-exhausted pool would be a security failure, so aborting via an exception is the correct response. Callers that generate keys (e.g., `randomSecretKey`) do not catch this exception, allowing it to propagate and halt the operation.

## Relationship to the Crypto Module

Alongside `secure_erase.h` and `RFC1751.h`, this header forms the foundational crypto utilities layer. The pattern used throughout the codebase — generate into a local buffer via `crypto_prng()`, construct the key object, then immediately call `secure_erase` on the buffer — illustrates the intended lifecycle: `csprng_engine` produces the raw entropy, and `secure_erase` ensures it does not linger in memory after use.