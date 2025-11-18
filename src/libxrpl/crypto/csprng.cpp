#include <xrpl/basics/contract.h>
#include <xrpl/crypto/csprng.h>

#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <random>
#include <stdexcept>

namespace ripple {

csprng_engine::csprng_engine()
{
    // This is not strictly necessary
    if (RAND_poll() != 1)
        Throw<std::runtime_error>("CSPRNG: Initial polling failed");
}

csprng_engine::~csprng_engine()
{
    // This cleanup function is not needed in newer versions of OpenSSL
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    RAND_cleanup();
#endif
}

void
csprng_engine::mix_entropy(void* buffer, std::size_t count)
{
    std::array<std::random_device::result_type, 128> entropy;

    {
        // On every platform we support, std::random_device
        // is non-deterministic and should provide some good
        // quality entropy.
        std::random_device rd;

        for (auto& e : entropy)
            e = rd();
    }

    std::lock_guard lock(mutex_);

    // We add data to the pool, but we conservatively assume that
    // it contributes no actual entropy.
    RAND_add(
        entropy.data(),
        entropy.size() * sizeof(std::random_device::result_type),
        0);

    if (buffer != nullptr && count != 0)
        RAND_add(buffer, count, 0);
}

void
csprng_engine::operator()(void* ptr, std::size_t count)
{
    // RAND_bytes is thread-safe on OpenSSL 1.1.0 and later when compiled
    // with thread support, so we don't need to grab a mutex.
    // https://mta.openssl.org/pipermail/openssl-users/2020-November/013146.html
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || !defined(OPENSSL_THREADS)
    std::lock_guard lock(mutex_);
#endif

    auto const result =
        RAND_bytes(reinterpret_cast<unsigned char*>(ptr), count);

    if (result != 1)
        Throw<std::runtime_error>("CSPRNG: Insufficient entropy");
}

csprng_engine::result_type
csprng_engine::operator()()
{
    result_type ret;
    (*this)(&ret, sizeof(result_type));
    return ret;
}

csprng_engine&
crypto_prng()
{
    static csprng_engine engine;
    return engine;
}

}  // namespace ripple
