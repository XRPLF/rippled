#include <xrpl/protocol/digest.h>

#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include <cstddef>

namespace xrpl {

OpensslRipemd160Hasher::OpensslRipemd160Hasher()
{
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
