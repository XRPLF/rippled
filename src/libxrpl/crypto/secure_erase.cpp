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
