# `src/libxrpl/crypto/csprng.cpp`

## Role in the System

This file implements the XRPL project's cryptographically secure pseudorandom number generator. It wraps OpenSSL's `RAND_bytes` family behind a C++ class (`csprng_engine`) that satisfies the C++ standard `UniformRandomNumberEngine` named requirement, and exposes a global singleton through `crypto_prng()`. Nearly every security-sensitive operation in the codebase — key generation, seed creation, nonce production — flows through this single engine rather than scattering ad-hoc calls to OpenSSL directly.

## `csprng_engine` Design

The class is non-copyable and non-movable by explicit `= delete` on all copy/move operations. This is intentional: the engine holds a `std::mutex` member (`mutex_`) and owns a logical relationship with OpenSSL's internal entropy pool. Copying or moving it would produce dangerous aliasing with no meaningful semantics.

The constructor calls `RAND_poll()`, an OpenSSL function that asks the OS for fresh entropy (e.g., `/dev/urandom`, `getrandom()`, `CryptGenRandom`). While technically optional — OpenSSL will seed itself lazily on first use — the explicit call at construction time surfaces any platform-level entropy failure immediately at startup rather than silently at the first key generation. A failed `RAND_poll()` throws `std::runtime_error` via `Throw<>`, the project-wide helper from `contract.h` that logs a call stack before rethrowing.

The destructor conditionally calls `RAND_cleanup()`, but only when compiled against OpenSSL older than 1.1.0 (`OPENSSL_VERSION_NUMBER < 0x10100000L`). OpenSSL 1.1.0 deprecated this function because its cleanup is now handled automatically, and calling it explicitly on newer builds would be incorrect.

## Entropy Mixing

`mix_entropy()` exists to periodically stir additional randomness into the OpenSSL pool. It does two things: first, it allocates a stack array of 128 `std::random_device::result_type` values, fills them from `std::random_device`, and then feeds them to OpenSSL via `RAND_add`. Second, if the caller passes a non-null buffer, that data is also fed to `RAND_add`.

The notable detail is the third argument to every `RAND_add` call: `0`. This is the entropy estimate — the caller is telling OpenSSL "I'm giving you data but I'm not vouching for how many unpredictable bits it actually contains." This conservative stance avoids overestimating entropy quality, which could prematurely satisfy OpenSSL's seeding requirements and weaken the pool. In practice, `std::random_device` is non-deterministic on all supported XRPL platforms, but the code deliberately declines to rely on that guarantee.

`mix_entropy()` is called on a timer in `Application.cpp` — the production application stirs in fresh system entropy at regular intervals during the node's lifetime, not just at startup.

The mutex is acquired for the `RAND_add` calls but not for the `std::random_device` reads, which are done before locking. This is correct because `std::random_device` is independently thread-safe and there is no invariant tying the OS reads to the OpenSSL pool state.

## Thread Safety and the Version Guard

The bulk byte-generation operator `operator()(void* ptr, std::size_t count)` wraps `RAND_bytes()`, which fills a caller-supplied buffer with cryptographically secure random bytes. The thread-safety strategy here involves a compile-time branch:

```cpp
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || !defined(OPENSSL_THREADS)
    std::lock_guard lock(mutex_);
#endif
```

OpenSSL 1.1.0 made `RAND_bytes` internally thread-safe when built with thread support, making an external mutex redundant and costly. The guard retains safety on older OpenSSL or in single-threaded builds where `OPENSSL_THREADS` is absent. Rather than always paying the locking cost, the code uses preprocessor dispatch to let the compiler eliminate dead paths entirely.

The scalar `operator()()` — satisfying the `UniformRandomNumberEngine` `operator()` requirement that returns a single `result_type` — simply delegates to the buffer-filling overload with `sizeof(result_type)` (8 bytes, since `result_type` is `std::uint64_t`). This keeps the implementation DRY and ensures both overloads go through the same validation and error-handling path.

## Singleton Accessor

`crypto_prng()` returns a reference to a function-local `static csprng_engine`. The C++11 standard guarantees that function-local statics are initialized exactly once even in the presence of concurrent callers, making this a thread-safe Meyers singleton with no need for an explicit `std::once_flag` or double-checked locking. The singleton design also means all callers share a single OpenSSL entropy pool state, which is correct — multiple independent `csprng_engine` instances would each manage their own view of OpenSSL's global state while OpenSSL itself has only one, leading to unnecessary locking and confusion.

## Error Handling

Both failure points that can be detected — `RAND_poll()` returning non-1 in the constructor, and `RAND_bytes()` returning non-1 in the generation operator — throw `std::runtime_error` through `Throw<>`. Using `Throw<>` rather than a bare `throw` ensures the failure is logged with a stack trace before propagation, aiding post-mortem diagnostics. There is no silent fallback or retry: an insufficient-entropy condition is treated as unrecoverable because silently returning weak or repeated random data in a cryptographic context would be far more dangerous than crashing.