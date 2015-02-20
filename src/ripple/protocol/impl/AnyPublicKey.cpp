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

#include <ripple/protocol/AnyPublicKey.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/STExchange.h>
#include <ed25519-donna/ed25519.h>
#include <cassert>

namespace ripple {

/** Verify a secp256k1 signature. */
bool
verify_secp256k1 (void const* pk,
    void const* msg, std::size_t msg_size,
    void const* sig, std::size_t sig_size)
{
    return false;
}

bool
verify_ed25519 (void const* pk,
    void const* msg, std::size_t msg_size,
    void const* sig, std::size_t sig_size)
{
    if (sig_size != 64)
        return false;
    ed25519_public_key epk;
    ed25519_signature es;
    std::memcpy(epk, pk, 32);
    std::memcpy(es, sig, sig_size);
    return ed25519_sign_open(
        reinterpret_cast<unsigned char const*>(msg),
            msg_size, epk, es) == 0;
}

//------------------------------------------------------------------------------

KeyType
AnyPublicKeySlice::type() const noexcept
{
    auto const pk = data();
    auto const pk_size = size();

    if (pk_size < 1)
        return KeyType::unknown;
    auto const len = pk_size - 1;
    if (len == 32 &&
            pk[0] == 0xED)
        return KeyType::ed25519;
    if (len == 33 &&
            (pk[0] == 0x02 || pk[0] == 0x03))
        return KeyType::secp256k1;
    return KeyType::unknown;
}

bool
AnyPublicKeySlice::verify (
    void const* msg, std::size_t msg_size,
    void const* sig, std::size_t sig_size) const
{
    switch(type())
    {
    case KeyType::ed25519:
        return verify_ed25519(data() + 1,
            msg, msg_size, sig, sig_size);
    case KeyType::secp256k1:
        return verify_secp256k1(data() + 1,
            msg, msg_size, sig, sig_size);
    default:
        break;
    }
    // throw?
    return false;
}

} // ripple
