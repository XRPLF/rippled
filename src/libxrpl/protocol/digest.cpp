/** @file
 *  Concrete implementations of the RIPEMD-160, SHA-256, and SHA-512 hashers.
 *
 *  Each hasher stores its OpenSSL context in an opaque `char ctx_[]` buffer
 *  declared in the header so that `digest.h` does not need to expose any
 *  OpenSSL headers to its consumers.  The constructors here recover the real
 *  OpenSSL type via `reinterpret_cast`, which is only safe when the buffer
 *  size exactly matches `sizeof` the target struct.  A `static_assert` in
 *  each constructor enforces that invariant at compile time: if an OpenSSL
 *  upgrade ever changes a context struct's size, the build breaks here rather
 *  than silently corrupting memory at runtime.
 */

#include <xrpl/protocol/digest.h>

#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include <cstddef>

namespace xrpl {

OpensslRipemd160Hasher::OpensslRipemd160Hasher()
{
    // Compile-time firewall: ctx_ must be exactly sizeof(RIPEMD160_CTX) bytes
    // for the reinterpret_cast below to be safe.  If an OpenSSL upgrade ever
    // changes the size of RIPEMD160_CTX, this assert fires and the hardcoded
    // buffer size in digest.h must be updated to match.
    static_assert(sizeof(decltype(OpensslRipemd160Hasher::ctx_)) == sizeof(RIPEMD160_CTX), "");
    auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
    RIPEMD160_Init(ctx);
}

void
OpensslRipemd160Hasher::operator()(void const* data, std::size_t size) noexcept
{
    auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
    RIPEMD160_Update(ctx, data, size);
}

OpensslRipemd160Hasher::
operator result_type() noexcept
{
    auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
    result_type digest;
    RIPEMD160_Final(digest.data(), ctx);
    return digest;
}

//------------------------------------------------------------------------------

OpensslSha512Hasher::OpensslSha512Hasher()
{
    // Same opaque-buffer safety check as OpensslRipemd160Hasher: ctx_ must
    // match sizeof(SHA512_CTX).  Update the buffer size in digest.h if this
    // assert fires after an OpenSSL upgrade.
    static_assert(sizeof(decltype(OpensslSha512Hasher::ctx_)) == sizeof(SHA512_CTX), "");
    auto const ctx = reinterpret_cast<SHA512_CTX*>(ctx_);
    SHA512_Init(ctx);
}

void
OpensslSha512Hasher::operator()(void const* data, std::size_t size) noexcept
{
    auto const ctx = reinterpret_cast<SHA512_CTX*>(ctx_);
    SHA512_Update(ctx, data, size);
}

OpensslSha512Hasher::
operator result_type() noexcept
{
    auto const ctx = reinterpret_cast<SHA512_CTX*>(ctx_);
    result_type digest;
    SHA512_Final(digest.data(), ctx);
    return digest;
}

//------------------------------------------------------------------------------

OpensslSha256Hasher::OpensslSha256Hasher()
{
    // Same opaque-buffer safety check as OpensslRipemd160Hasher: ctx_ must
    // match sizeof(SHA256_CTX).  Update the buffer size in digest.h if this
    // assert fires after an OpenSSL upgrade.
    static_assert(sizeof(decltype(OpensslSha256Hasher::ctx_)) == sizeof(SHA256_CTX), "");
    auto const ctx = reinterpret_cast<SHA256_CTX*>(ctx_);
    SHA256_Init(ctx);
}

void
OpensslSha256Hasher::operator()(void const* data, std::size_t size) noexcept
{
    auto const ctx = reinterpret_cast<SHA256_CTX*>(ctx_);
    SHA256_Update(ctx, data, size);
}

OpensslSha256Hasher::
operator result_type() noexcept
{
    auto const ctx = reinterpret_cast<SHA256_CTX*>(ctx_);
    result_type digest;
    SHA256_Final(digest.data(), ctx);
    return digest;
}

}  // namespace xrpl
