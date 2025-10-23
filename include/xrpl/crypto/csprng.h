#ifndef XRPL_CRYPTO_RANDOM_H_INCLUDED
#define XRPL_CRYPTO_RANDOM_H_INCLUDED

#include <mutex>

namespace ripple {

/** A cryptographically secure random number engine

    The engine is thread-safe (it uses a lock to serialize
    access) and will, automatically, mix in some randomness
    from std::random_device.

    Meets the requirements of UniformRandomNumberEngine
*/
class csprng_engine
{
private:
    std::mutex mutex_;

public:
    using result_type = std::uint64_t;

    csprng_engine(csprng_engine const&) = delete;
    csprng_engine&
    operator=(csprng_engine const&) = delete;

    csprng_engine(csprng_engine&&) = delete;
    csprng_engine&
    operator=(csprng_engine&&) = delete;

    csprng_engine();
    ~csprng_engine();

    /** Mix entropy into the pool */
    void
    mix_entropy(void* buffer = nullptr, std::size_t count = 0);

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
csprng_engine&
crypto_prng();

}  // namespace ripple

#endif
