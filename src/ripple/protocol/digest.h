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

#include <beast/crypto/ripemd.h>
#include <beast/crypto/sha2.h>
#include <beast/hash/endian.h>
#include <beast/utility/noexcept.h>
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
    static beast::endian const endian =
        beast::endian::native;

    using result_type =
        std::array<std::uint8_t, 20>;

    openssl_ripemd160_hasher();

    void
    operator()(void const* data,
        std::size_t size) noexcept;

    explicit
    operator result_type() noexcept;

private:
    char ctx_[96];
};

/** SHA-256 digest

    @note This uses the OpenSSL implementation
*/
struct openssl_sha256_hasher
{
public:
    static beast::endian const endian =
        beast::endian::native;

    using result_type =
        std::array<std::uint8_t, 32>;

    openssl_sha256_hasher();

    void
    operator()(void const* data,
        std::size_t size) noexcept;

    explicit
    operator result_type() noexcept;

private:
    char ctx_[112];
};

//------------------------------------------------------------------------------

// Aliases to choose the correct digest implementation

#if RIPPLE_USE_OPENSSL
using ripemd160_hasher = openssl_ripemd160_hasher;
using sha256_hasher = openssl_sha256_hasher;
#else
using ripemd160_hasher = beast::ripemd160_hasher;
using sha256_hasher = beast::sha256_hasher;
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

    @param digest A buffer of at least 20 bytes
*/
struct ripesha_hasher
{
private:
    sha256_hasher h_;

public:
    static beast::endian const endian =
        beast::endian::native;

    using result_type =
        std::array<std::uint8_t, 20>;

    void
    operator()(void const* data,
        std::size_t size) noexcept
    {
        h_(data, size);
    }

    explicit
    operator result_type() noexcept
    {
        auto const d0 = static_cast<
            decltype(h_)::result_type>(h_);
        ripemd160_hasher rh;
        rh(d0.data(), d0.size());
        return  static_cast<
            decltype(rh)::result_type>(rh);
    }
};

} // ripple

#endif
