//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_DIGEST_H_INCLUDED
#define RIPPLE_PROTOCOL_DIGEST_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/beast/crypto/ripemd.h>
#include <ripple/beast/crypto/sha2.h>
#include <ripple/beast/hash/endian.h>
#include <algorithm>
#include <array>

namespace ripple {

/*  Message digest functions used in the Ripple Protocol

    Modeled to meet the requirements of `Hasher` in the
    `hash_append` interface, currently in proposal:

    N3980 "Types Don't Know #"
    http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3980.html
*/

//------------------------------------------------------------------------------

/** RIPEMD-160 digest

    @note This uses the OpenSSL implementation
*/
struct openssl_ripemd160_hasher
{
public:
    static beast::endian const endian = beast::endian::native;

    using result_type = std::array<std::uint8_t, 20>;

    openssl_ripemd160_hasher();

    void
    operator()(void const* data, std::size_t size) noexcept;

    explicit operator result_type() noexcept;

private:
    char ctx_[96];
};

/** SHA-512 digest

    @note This uses the OpenSSL implementation
*/
struct openssl_sha512_hasher
{
public:
    static beast::endian const endian = beast::endian::native;

    using result_type = std::array<std::uint8_t, 64>;

    openssl_sha512_hasher();

    void
    operator()(void const* data, std::size_t size) noexcept;

    explicit operator result_type() noexcept;

private:
    char ctx_[216];
};

/** SHA-256 digest

    @note This uses the OpenSSL implementation
*/
struct openssl_sha256_hasher
{
public:
    static beast::endian const endian = beast::endian::native;

    using result_type = std::array<std::uint8_t, 32>;

    openssl_sha256_hasher();

    void
    operator()(void const* data, std::size_t size) noexcept;

    explicit operator result_type() noexcept;

private:
    char ctx_[112];
};

//------------------------------------------------------------------------------

// Aliases to choose the correct digest implementation

#if USE_BEAST_HASHER
using ripemd160_hasher = beast::ripemd160_hasher;
using sha256_hasher = beast::sha256_hasher;
using sha512_hasher = beast::sha512_hasher;
#else
using ripemd160_hasher = openssl_ripemd160_hasher;
using sha256_hasher = openssl_sha256_hasher;
using sha512_hasher = openssl_sha512_hasher;
#endif

//------------------------------------------------------------------------------

/** Returns the RIPEMD-160 digest of the SHA256 hash of the message.

    This operation is used to compute the 160-bit identifier
    representing a Ripple account, from a message. Typically the
    message is the public key of the account - which is not
    stored in the account root.

    The same computation is used regardless of the cryptographic
    scheme implied by the public key. For example, the public key
    may be an ed25519 public key or a secp256k1 public key. Support
    for new cryptographic systems may be added, using the same
    formula for calculating the account identifier.

    Meets the requirements of Hasher (in hash_append)
*/
struct ripesha_hasher
{
private:
    sha256_hasher h_;

public:
    static beast::endian const endian = beast::endian::native;

    using result_type = std::array<std::uint8_t, 20>;

    void
    operator()(void const* data, std::size_t size) noexcept
    {
        h_(data, size);
    }

    explicit operator result_type() noexcept
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
struct basic_sha512_half_hasher
{
private:
    sha512_hasher h_;

public:
    static beast::endian const endian = beast::endian::big;

    using result_type = uint256;

    ~basic_sha512_half_hasher()
    {
        erase(std::integral_constant<bool, Secure>{});
    }

    void
    operator()(void const* data, std::size_t size) noexcept
    {
        h_(data, size);
    }

    explicit operator result_type() noexcept
    {
        auto const digest = sha512_hasher::result_type(h_);
        result_type result;
        std::copy(digest.begin(), digest.begin() + 32, result.begin());
        return result;
    }

private:
    inline void erase(std::false_type)
    {
    }

    inline void erase(std::true_type)
    {
        beast::secure_erase(&h_, sizeof(h_));
    }
};

}  // namespace detail

using sha512_half_hasher = detail::basic_sha512_half_hasher<false>;

// secure version
using sha512_half_hasher_s = detail::basic_sha512_half_hasher<true>;

//------------------------------------------------------------------------------

#ifdef _MSC_VER
// Call from main to fix magic statics pre-VS2015
inline void
sha512_deprecatedMSVCWorkaround()
{
    beast::sha512_hasher h;
    auto const digest = static_cast<beast::sha512_hasher::result_type>(h);
}
#endif

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
sha512Half_s(Args const&... args)
{
    sha512_half_hasher_s h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename sha512_half_hasher_s::result_type>(h);
}

}  // namespace ripple

#endif
