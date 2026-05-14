/** @file
 *  Implements `xrpl::secureErase` as a one-line delegation to
 *  `OPENSSL_cleanse`.
 *
 *  The deliberate isolation of this call in its own translation unit is the
 *  key design decision: because the definition is opaque to every call site,
 *  the compiler cannot prove the write is dead and therefore cannot eliminate
 *  it.  A naive `memset` inlined at the call site would be removed by any
 *  optimising compiler once it determines the buffer is never read again,
 *  leaving secret key material in memory.  The separate-TU placement enforces
 *  the same effect as a `volatile` write across all supported compilers and
 *  platforms without requiring platform-specific APIs (`memset_s`,
 *  `explicit_bzero`, `SecureZeroMemory`).
 *
 *  `OPENSSL_cleanse` itself may additionally use volatile stores or opaque
 *  function-pointer dispatch internally, but the separate-TU guarantee is the
 *  primary defence here.
 */

#include <xrpl/crypto/secure_erase.h>

#include <openssl/crypto.h>

#include <cstddef>

namespace xrpl {

void
secureErase(void* dest, std::size_t bytes)
{
    OPENSSL_cleanse(dest, bytes);
}

}  // namespace xrpl
