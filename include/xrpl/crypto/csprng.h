#pragma once

#include <mutex>

namespace xrpl {

/** @file
 *  Cryptographically secure pseudo-random number engine and singleton accessor.
 *
 *  Every piece of key material in the XRP Ledger — wallet seeds, secret keys,
 *  nonces, session identifiers — is generated through `CsprngEngine`. The class
 *  is a thin, type-safe C++ wrapper around OpenSSL's `RAND_bytes` that provides
 *  thread safety and satisfies the C++ *UniformRandomNumberEngine* named
 *  requirement, allowing it to be used directly with standard-library facilities
 *  such as `std::uniform_int_distribution` and `beast::rngfill`.
 */

/** Cryptographically secure random number engine backed by OpenSSL.
 *
 *  Wraps OpenSSL's `RAND_bytes` to provide randomness to the rest of the
 *  codebase without any caller needing to touch OpenSSL directly. Satisfies
 *  the C++ *UniformRandomNumberEngine* named requirement (`result_type`,
 *  `operator()()`, `min()`, `max()`), so it plugs directly into
 *  `std::uniform_int_distribution`, `beast::rngfill`, and similar utilities.
 *
 *  Thread safety is version-conditioned at compile time: on OpenSSL ≥ 1.1.0
 *  built with thread support, `RAND_bytes` is internally thread-safe and the
 *  per-call mutex acquisition is elided on the hot path. On older OpenSSL the
 *  mutex is always held. Entropy mixing (`mixEntropy`) always holds the mutex
 *  regardless of OpenSSL version because `RAND_add` modifies shared pool state.
 *
 *  Copy and move operations are deleted. The engine holds a `std::mutex`, is
 *  backed by a global OpenSSL PRNG pool, and must be accessed as a singleton.
 *  Copying would produce a second object with no coherent relationship to that
 *  shared state. Use `cryptoPrng()` to obtain the singleton reference.
 *
 *  @see cryptoPrng()
 */
class CsprngEngine
{
private:
    std::mutex mutex_;

public:
    using result_type = std::uint64_t;

    CsprngEngine(CsprngEngine const&) = delete;
    CsprngEngine&
    operator=(CsprngEngine const&) = delete;

    CsprngEngine(CsprngEngine&&) = delete;
    CsprngEngine&
    operator=(CsprngEngine&&) = delete;

    /** Construct and eagerly seed the engine.
     *
     *  Calls `RAND_poll()` to harvest OS entropy (e.g., `/dev/urandom` on
     *  Linux, `CryptGenRandom` on Windows) before any bytes are generated.
     *  Although OpenSSL seeds itself lazily on first use, polling eagerly
     *  surfaces seeding failures at startup rather than during key generation.
     *
     *  @throw std::runtime_error if `RAND_poll()` fails.
     */
    CsprngEngine();

    /** Destroy the engine, releasing OpenSSL PRNG state on older runtimes.
     *
     *  Calls `RAND_cleanup()` only for OpenSSL versions older than 1.1.0.
     *  Modern OpenSSL manages cleanup internally via `atexit`; calling
     *  `RAND_cleanup()` on those versions is unnecessary and was removed.
     */
    ~CsprngEngine();

    /** Stir additional entropy into the OpenSSL random pool.
     *
     *  Reads 128 values from `std::random_device` and passes them to
     *  `RAND_add` with an entropy estimate of zero. The caller-supplied
     *  buffer, if provided, is also added with a zero entropy estimate.
     *  The zero estimate is deliberate: on some platforms `std::random_device`
     *  may fall back to a software PRNG, so claiming zero entropy ensures
     *  OpenSSL's internal seeding threshold is never prematurely satisfied by
     *  potentially weak input. The data is still mixed into the pool.
     *
     *  Called periodically from `Application.cpp` to stir in fresh OS entropy
     *  during the node's lifetime. May also be called with caller-supplied
     *  high-quality entropy from a hardware RNG or other trusted source.
     *
     *  @param buffer Optional pointer to additional entropy material to mix in.
     *      Ignored if `nullptr` or if `count` is zero.
     *  @param count  Number of bytes at `buffer` to mix in.
     */
    void
    mixEntropy(void* buffer = nullptr, std::size_t count = 0);

    /** Generate a single random `result_type` value.
     *
     *  Delegates to the buffer-fill overload with `sizeof(result_type)` bytes,
     *  sharing the same validation and error-handling path.
     *
     *  @return A uniformly distributed random `std::uint64_t`.
     *  @throw std::runtime_error if the underlying `RAND_bytes` call fails
     *      (e.g., entropy pool exhausted). This is an unrecoverable condition;
     *      the exception is not caught by callers such as `randomSecretKey()`.
     */
    result_type
    operator()();

    /** Fill a buffer with cryptographically secure random bytes.
     *
     *  On OpenSSL ≥ 1.1.0 (built with thread support) the call to `RAND_bytes`
     *  is internally thread-safe and the mutex is elided at compile time. On
     *  older OpenSSL the mutex is held for the duration of the call.
     *
     *  @param ptr   Pointer to the buffer to fill; must not be `nullptr` when
     *      `count` is non-zero.
     *  @param count Number of random bytes to write into `ptr`.
     *  @throw std::runtime_error ("CSPRNG: Insufficient entropy") if
     *      `RAND_bytes` returns anything other than 1. Generating key material
     *      from an exhausted pool is a security failure, so the exception
     *      propagates and halts the operation.
     */
    void
    operator()(void* ptr, std::size_t count);

    /** Return the smallest value that `operator()()` can produce.
     *
     *  Required by the *UniformRandomNumberEngine* named requirement.
     *  Always returns `std::numeric_limits<result_type>::min()`.
     */
    static constexpr result_type
    min()
    {
        return std::numeric_limits<result_type>::min();
    }

    /** Return the largest value that `operator()()` can produce.
     *
     *  Required by the *UniformRandomNumberEngine* named requirement.
     *  Always returns `std::numeric_limits<result_type>::max()`.
     */
    static constexpr result_type
    max()
    {
        return std::numeric_limits<result_type>::max();
    }
};

/** Return a reference to the process-wide cryptographically secure PRNG.
 *
 *  Use this whenever random numbers or bytes are needed for cryptographic
 *  purposes: key generation, seed creation, nonce production, or any value
 *  passed into a cryptographic routine. The returned engine satisfies the
 *  C++ *UniformRandomNumberEngine* requirement and can be used directly with
 *  `std::uniform_int_distribution`, `beast::rngfill`, and similar utilities.
 *
 *  The singleton is a Meyers-static local; C++11 guarantees thread-safe
 *  one-time construction, so the first call from any thread safely initialises
 *  the engine exactly once. Every caller shares the same OpenSSL PRNG pool.
 *
 *  @return Reference to the process-wide `CsprngEngine` singleton.
 *  @note Never copy or store the returned reference by value — the deleted
 *      copy/move operations on `CsprngEngine` prevent this at compile time.
 *  @see CsprngEngine
 */
CsprngEngine&
cryptoPrng();

}  // namespace xrpl
