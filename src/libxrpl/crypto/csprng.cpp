/** @file
 *  Implements the process-wide cryptographically secure PRNG.
 *
 *  Every security-sensitive operation in xrpld — key generation, seed
 *  creation, nonce production — flows through the `CsprngEngine` singleton
 *  returned by `cryptoPrng()`.  The implementation wraps OpenSSL's
 *  `RAND_bytes` / `RAND_add` / `RAND_poll` family and keeps all callers
 *  insulated from OpenSSL API differences across versions.
 */
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
    // Eagerly poll for OS entropy so that any platform-level seeding failure
    // surfaces at startup rather than silently at the first key generation.
    if (RAND_poll() != 1)
        Throw<std::runtime_error>("CSPRNG: Initial polling failed");
}

CsprngEngine::~CsprngEngine()
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    RAND_cleanup();
#endif
}

void
CsprngEngine::mixEntropy(void* buffer, std::size_t count)
{
    std::array<std::random_device::result_type, 128> entropy{};

    {
        std::random_device rd;

        for (auto& e : entropy)
            e = rd();
    }

    std::scoped_lock const lock(mutex_);

    // Entropy estimate is 0 for both RAND_add calls: we deliberately decline
    // to credit OpenSSL's seeding threshold, avoiding premature satisfaction
    // of the threshold if std::random_device falls back to a software PRNG.
    RAND_add(entropy.data(), entropy.size() * sizeof(std::random_device::result_type), 0);

    if (buffer != nullptr && count != 0)
        RAND_add(buffer, count, 0);
}

void
CsprngEngine::operator()(void* ptr, std::size_t count)
{
    // RAND_bytes is internally thread-safe on OpenSSL ≥ 1.1.0 built with
    // thread support; the external mutex is only needed on older builds.
    // See: https://mta.openssl.org/pipermail/openssl-users/2020-November/013146.html
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
    static CsprngEngine kENGINE;
    return kENGINE;
}

}  // namespace xrpl
