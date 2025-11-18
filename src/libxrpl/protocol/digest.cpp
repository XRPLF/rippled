#include <xrpl/protocol/digest.h>

#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include <cstddef>

namespace ripple {

openssl_ripemd160_hasher::openssl_ripemd160_hasher()
{
    static_assert(
        sizeof(decltype(openssl_ripemd160_hasher::ctx_)) ==
            sizeof(RIPEMD160_CTX),
        "");
    auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
    RIPEMD160_Init(ctx);
}

void
openssl_ripemd160_hasher::operator()(
    void const* data,
    std::size_t size) noexcept
{
    auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
    RIPEMD160_Update(ctx, data, size);
}

openssl_ripemd160_hasher::operator result_type() noexcept
{
    auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
    result_type digest;
    RIPEMD160_Final(digest.data(), ctx);
    return digest;
}

//------------------------------------------------------------------------------

openssl_sha512_hasher::openssl_sha512_hasher()
{
    static_assert(
        sizeof(decltype(openssl_sha512_hasher::ctx_)) == sizeof(SHA512_CTX),
        "");
    auto const ctx = reinterpret_cast<SHA512_CTX*>(ctx_);
    SHA512_Init(ctx);
}

void
openssl_sha512_hasher::operator()(void const* data, std::size_t size) noexcept
{
    auto const ctx = reinterpret_cast<SHA512_CTX*>(ctx_);
    SHA512_Update(ctx, data, size);
}

openssl_sha512_hasher::operator result_type() noexcept
{
    auto const ctx = reinterpret_cast<SHA512_CTX*>(ctx_);
    result_type digest;
    SHA512_Final(digest.data(), ctx);
    return digest;
}

//------------------------------------------------------------------------------

openssl_sha256_hasher::openssl_sha256_hasher()
{
    static_assert(
        sizeof(decltype(openssl_sha256_hasher::ctx_)) == sizeof(SHA256_CTX),
        "");
    auto const ctx = reinterpret_cast<SHA256_CTX*>(ctx_);
    SHA256_Init(ctx);
}

void
openssl_sha256_hasher::operator()(void const* data, std::size_t size) noexcept
{
    auto const ctx = reinterpret_cast<SHA256_CTX*>(ctx_);
    SHA256_Update(ctx, data, size);
}

openssl_sha256_hasher::operator result_type() noexcept
{
    auto const ctx = reinterpret_cast<SHA256_CTX*>(ctx_);
    result_type digest;
    SHA256_Final(digest.data(), ctx);
    return digest;
}

}  // namespace ripple
