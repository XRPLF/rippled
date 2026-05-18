# `include/xrpl/basics/random.h`

This header provides the XRPL ledger's general-purpose pseudo-random number generation layer. It deliberately occupies a narrow scope: fast, deterministic, thread-safe non-cryptographic randomness for simulation, jitter, test data, and protocol logic that does not require unpredictability guarantees. It is explicitly excluded from any use in key generation, IVs, or security-sensitive contexts.

## The Engine: `beast::xor_shift_engine`

The underlying generator is `beast::xor_shift_engine`, an xorshift128+ implementation with a `uint64_t` result type. The algorithm maintains two 64-bit state words, advances them with XOR-shift operations per the Vigna reference implementation, and mixes the seed through MurmurHash3 finalizer constants (`0xff51afd7ed558ccd` and `0xc4ceb9fe1a85ec53`) to ensure the state is well-distributed even from low-entropy seeds. The engine satisfies C++11's `UniformRandomBitGenerator` concept — it provides `min()`, `max()`, and `operator()` — making it compatible with `std::uniform_int_distribution` and all standard distribution types.

Two `static_assert`s at namespace scope guard the engine contract: the result type must be unsigned integral, and its maximum must be at least as large as `uint64_t::max`. These are guarded with `#ifndef __INTELLISENSE__` to suppress false-positive diagnostic noise in IDEs, which sometimes fail to evaluate constant expressions across template instantiation boundaries.

## `default_prng()`: A Two-Level Seeding Hierarchy

The design challenge for thread-local PRNGs is giving each thread a distinct, high-quality seed without paying the cost of `std::random_device` on every thread startup (which can be slow or even blocking on some systems). `default_prng()` solves this with a two-level hierarchy:

1. A single `static beast::xor_shift_engine seeder` is initialized once from `std::random_device`, which provides true entropy at program startup. A `static std::mutex` serializes all accesses to this seeder.
2. Each thread gets a `thread_local beast::xor_shift_engine engine` that is seeded lazily on first access by drawing one value from `seeder` under the mutex lock. After initialization, the thread-local engine runs entirely independently with zero contention.

This approach ensures that threads never share RNG state, avoiding the need for per-call locking while still guaranteeing statistically independent sequences across threads. The `std::uniform_int_distribution<uint64_t>` with lower bound `1` is used when seeding to respect the engine's constraint that seed zero is invalid (which would throw `std::domain_error` in `xor_shift_engine::seed()`).

## `rand_int`: Overload Family

The `rand_int` family provides six overloads covering every combination of: with/without explicit engine, with/without min, with/without max. All are constrained with `std::enable_if_t<std::is_integral<Integral>::value>` to prevent misuse with floating-point or enum types. The engine-taking variants additionally require `detail::is_engine<Engine>::value`, which is a type alias for `std::is_invocable_r<Result, Engine>` — a minimal duck-typing check that the type can be called with no arguments and returns its `result_type`. All overloads delegate to `std::uniform_int_distribution`, and the comment in the implementation acknowledges that constructing the distribution object should be negligible cost, with a note to optimize if profiling reveals otherwise.

The `XRPL_ASSERT` on the two-argument form checks `max > min` (strict), which is slightly tighter than `uniform_int_distribution`'s requirement of `min <= max`. Equal bounds are not supported through this API, which is a defensible choice since calling `rand_int(5, 5)` is almost certainly a bug.

## `rand_byte` and `rand_bool`

`rand_byte` is constrained to exactly `unsigned char` or `uint8_t` (not any integral type) via a conjunction in `std::enable_if_t`. The internal implementation routes through `rand_int<Engine, std::uint32_t>` rather than `rand_int<Engine, Byte>` — this sidesteps potential implementation-defined behavior in `std::uniform_int_distribution<uint8_t>` on platforms where the standard library is not required to handle byte-wide integer distributions.

`rand_bool` is the simplest primitive: `rand_int(engine, 1) == 1`, producing a fair coin flip. The symmetric phrasing (rather than `rand_int(engine, 1) != 0`) is intentional for clarity.

## Usage Context

The header is included across networking subsystems (`peerfinder`, `overlay`), consensus code, HTTP/WebSocket work queues, and extensively throughout the test suite. In production code the role is typically non-security jitter — for example, randomizing peer selection order or staggering reconnect timers — while tests use the engine-taking overloads with deterministic seeds to produce reproducible random data. The engine-taking overloads exist precisely to support this dual use: callers that need reproducibility pass their own seeded engine; callers that just want "some randomness" omit it and get the thread-local default.