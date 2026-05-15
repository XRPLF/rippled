#include <xrpl/crypto/csprng.h>

#include <xrpl/basics/contract.h>

#include <openssl/opensslv.h>
#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <random>
#include <stdexcept>

namespace xrpl {

CsprngEngine::CsprngEngine()
{
    // This is not strictly necessary
    if (RAND_poll() != 1)
        Throw<std::runtime_error>("CSPRNG: Initial polling failed");
}

CsprngEngine::~CsprngEngine()
{
    // This cleanup function is not needed in newer versions of OpenSSL
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    RAND_cleanup();
#endif
}

void
CsprngEngine::mixEntropy(void* buffer, std::size_t count)
{
    std::array<std::random_device::result_type, 128> entropy{};

    {
        // On every platform we support, std::random_device
        // is non-deterministic and should provide some good
        // quality entropy.
        std::random_device rd;

        for (auto& e : entropy)
            e = rd();
    }

    std::scoped_lock const lock(mutex_);

    // We add data to the pool, but we conservatively assume that
    // it contributes no actual entropy.
    RAND_add(entropy.data(), entropy.size() * sizeof(std::random_device::result_type), 0);

    if (buffer != nullptr && count != 0)
        RAND_add(buffer, count, 0);
}

void
CsprngEngine::operator()(void* ptr, std::size_t count)
{
    // RAND_bytes is thread-safe on OpenSSL 1.1.0 and later when compiled
    // with thread support, so we don't need to grab a mutex.
    // https://mta.openssl.org/pipermail/openssl-users/2020-November/013146.html
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || !defined(OPENSSL_THREADS)
    std::scoped_lock lock(mutex_);
#endif

    auto const result = RAND_bytes(reinterpret_cast<unsigned char*>(ptr), count);

    if (result != 1)
        Throw<std::runtime_error>("CSPRNG: Insufficient entropy");
}

CsprngEngine::result_type
CsprngEngine::operator()()
{
    result_type ret = 0;
    (*this)(&ret, sizeof(result_type));
    return ret;
}

CsprngEngine&
cryptoPrng()
{
    static CsprngEngine kEngine;
    return kEngine;
}

}  // namespace xrpl
