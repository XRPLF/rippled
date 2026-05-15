#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/crypto/secure_erase.h>

#include <boost/endian/conversion.hpp>

#include <array>

namespace xrpl {

/** Message digest functions used in the codebase

    @note These are modeled to meet the requirements of `Hasher` in the
          `hash_append` interface, discussed in proposal:

          N3980 "Types Don't Know #"
          http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3980.html
*/

//------------------------------------------------------------------------------

/** RIPEMD-160 digest

    @note This uses the OpenSSL implementation
*/
struct OpensslRipemd160Hasher
{
public:
    static constexpr auto kEndian = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 20>;

    OpensslRipemd160Hasher();

    void
    operator()(void const* data, std::size_t size) noexcept;

    explicit
    operator result_type() noexcept;

private:
    char ctx_[96]{};
};

/** SHA-512 digest

    @note This uses the OpenSSL implementation
*/
struct OpensslSha512Hasher
{
public:
    static constexpr auto kEndian = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 64>;

    OpensslSha512Hasher();

    void
    operator()(void const* data, std::size_t size) noexcept;

    explicit
    operator result_type() noexcept;

private:
    char ctx_[216]{};
};

/** SHA-256 digest

    @note This uses the OpenSSL implementation
*/
struct OpensslSha256Hasher
{
public:
    static constexpr auto kEndian = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 32>;

    OpensslSha256Hasher();

    void
    operator()(void const* data, std::size_t size) noexcept;

    explicit
    operator result_type() noexcept;

private:
    char ctx_[112]{};
};

//------------------------------------------------------------------------------

using ripemd160_hasher = OpensslRipemd160Hasher;
using sha256_hasher = OpensslSha256Hasher;
using sha512_hasher = OpensslSha512Hasher;

//------------------------------------------------------------------------------

/** Returns the RIPEMD-160 digest of the SHA256 hash of the message.

    This operation is used to compute the 160-bit identifier
    representing an XRPL account, from a message. Typically the
    message is the public key of the account - which is not
    stored in the account root.

    The same computation is used regardless of the cryptographic
    scheme implied by the public key. For example, the public key
    may be an ed25519 public key or a secp256k1 public key. Support
    for new cryptographic systems may be added, using the same
    formula for calculating the account identifier.

    Meets the requirements of Hasher (in hash_append)
*/
struct RipeshaHasher
{
private:
    sha256_hasher h_;

public:
    static constexpr auto kEndian = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 20>;

    void
    operator()(void const* data, std::size_t size) noexcept
    {
        h_(data, size);
    }

    explicit
    operator result_type() noexcept
    {
        auto const d0 = sha256_hasher::result_type(h_);
        ripemd160_hasher rh;
        rh(d0.data(), d0.size());
        return ripemd160_hasher::result_type(rh);
    }
};

//------------------------------------------------------------------------------

namespace detail {

/** Returns the SHA512-Half digest of a message.

    The SHA512-Half is the first 256 bits of the
    SHA-512 digest of the message.
*/
template <bool Secure>
struct BasicSha512HalfHasher
{
private:
    sha512_hasher h_;

public:
    static constexpr auto kEndian = boost::endian::order::big;

    using result_type = uint256;

    ~BasicSha512HalfHasher()
    {
        erase(std::integral_constant<bool, Secure>{});
    }

    void
    operator()(void const* data, std::size_t size) noexcept
    {
        h_(data, size);
    }

    explicit
    operator result_type() noexcept
    {
        auto const digest = sha512_hasher::result_type(h_);
        return result_type::fromVoid(digest.data());
    }

private:
    void
    erase(std::false_type)
    {
    }

    void
    erase(std::true_type)
    {
        secureErase(&h_, sizeof(h_));
    }
};

}  // namespace detail

using sha512_half_hasher = detail::BasicSha512HalfHasher<false>;

// secure version
using sha512_half_hasher_s = detail::BasicSha512HalfHasher<true>;

//------------------------------------------------------------------------------

/** Returns the SHA512-Half of a series of objects. */
template <class... Args>
sha512_half_hasher::result_type
sha512Half(Args const&... args)
{
    sha512_half_hasher h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename sha512_half_hasher::result_type>(h);
}

/** Returns the SHA512-Half of a series of objects.

    Postconditions:
        Temporary memory storing copies of
        input messages will be cleared.
*/
template <class... Args>
sha512_half_hasher_s::result_type
sha512HalfS(Args const&... args)
{
    sha512_half_hasher_s h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename sha512_half_hasher_s::result_type>(h);
}

}  // namespace xrpl
