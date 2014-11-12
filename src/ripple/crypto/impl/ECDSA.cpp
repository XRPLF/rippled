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

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <ripple/crypto/ECDSA.h>
#include <ripple/crypto/ECDSACanonical.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/hmac.h>

namespace ripple  {

using openssl::ec_key;

static EC_KEY* new_initialized_EC_KEY()
{
    EC_KEY* key = EC_KEY_new_by_curve_name (NID_secp256k1);

    if (key == nullptr)
    {
        throw std::runtime_error ("new_initialized_EC_KEY() : EC_KEY_new_by_curve_name failed");
    }

    EC_KEY_set_conv_form (key, POINT_CONVERSION_COMPRESSED);

    return key;
}

ec_key ECDSAPrivateKey (uint256 const& serialized)
{
    EC_KEY* key = new_initialized_EC_KEY();

    BIGNUM* bn = BN_bin2bn (serialized.begin(), serialized.size(), nullptr);

    if (bn == nullptr)
    {
        // leaks key
        throw std::runtime_error ("ec_key::ec_key: BN_bin2bn failed");
    }

    const bool ok = EC_KEY_set_private_key (key, bn);

    BN_clear_free (bn);

    if (! ok)
    {
        EC_KEY_free (key);
    }

    return ec_key::acquire ((ec_key::pointer_t) key);
}

ec_key ECDSAPublicKey (Blob const& serialized)
{
    EC_KEY* key = new_initialized_EC_KEY();

    uint8_t const* begin = &serialized[0];

    if (o2i_ECPublicKey (&key, &begin, serialized.size()) != nullptr)
    {
        EC_KEY_set_conv_form (key, POINT_CONVERSION_COMPRESSED);
    }
    else
    {
        EC_KEY_free (key);
    }

    return ec_key::acquire ((ec_key::pointer_t) key);
}

Blob ECDSASign (uint256 const& hash, const openssl::ec_key& key)
{
    Blob result;

    unsigned char sig[128];
    unsigned int  siglen = sizeof sig - 1;

    const unsigned char* p = hash.begin();

    if (ECDSA_sign (0, p, hash.size(), sig, &siglen, (EC_KEY*) key.get()))
    {
        size_t newlen = siglen;

        makeCanonicalECDSASig (sig, newlen);

        result.resize (newlen);
        memcpy (&result[0], sig, newlen);
    }

    return result;
}

static bool ECDSAVerify (uint256 const& hash, uint8 const* sig, size_t sigLen, EC_KEY* key)
{
    // -1 = error, 0 = bad sig, 1 = good
    return ECDSA_verify (0, hash.begin(), hash.size(), sig, sigLen, key) > 0;
}

bool ECDSAVerify (uint256 const& hash, Blob const& sig, const openssl::ec_key& key)
{
    return ECDSAVerify (hash, sig.data(), sig.size(), (EC_KEY*) key.get());
}

} // ripple
