#include <xrpl/crypto/secure_erase.h>
#include <xrpl/basics/TraceLog.h>

#include <openssl/crypto.h>

#include <cstddef>

namespace xrpl {

void
secureErase(void* dest, std::size_t bytes)
{
    TRACE_FUNC();
    OPENSSL_cleanse(dest, bytes);
}

}  // namespace xrpl
