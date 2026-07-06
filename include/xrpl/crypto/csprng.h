#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

namespace xrpl {

/** A cryptographically secure random number engine

    The engine is thread-safe (it uses a lock to serialize
    access) and will, automatically, mix in some randomness
    from std::random_device.

    Meets the requirements of UniformRandomNumberEngine
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

    CsprngEngine();
    ~CsprngEngine();

    /** Mix entropy into the pool */
    void
    mixEntropy(void* buffer = nullptr, std::size_t count = 0);

    /** Generate a random integer */
    result_type
    operator()();

    /** Fill a buffer with the requested amount of random data */
    void
    operator()(void* ptr, std::size_t count);

    /* The smallest possible value that can be returned */
    static constexpr result_type
    min()
    {
        return std::numeric_limits<result_type>::min();
    }

    /* The largest possible value that can be returned */
    static constexpr result_type
    max()
    {
        return std::numeric_limits<result_type>::max();
    }
};

/** The default cryptographically secure PRNG

    Use this when you need to generate random numbers or
    data that will be used for encryption or passed into
    cryptographic routines.

    This meets the requirements of UniformRandomNumberEngine
*/
CsprngEngine&
cryptoPrng();

}  // namespace xrpl
