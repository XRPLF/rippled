#include <xrpl/crypto/secure_erase.h>

#include <openssl/crypto.h>

#include <cstddef>

namespace ripple {

void
secure_erase(void* dest, std::size_t bytes)
{
    OPENSSL_cleanse(dest, bytes);
}

}  // namespace ripple
