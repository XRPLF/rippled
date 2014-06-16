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

namespace ripple {

// #define EC_DEBUG

// Functions to add CKey support for deterministic EC keys

// <-- seed
uint128 CKey::PassPhraseToKey (const std::string& passPhrase)
{
    Serializer s;

    s.addRaw (passPhrase);
    // NIKB TODO this caling sequence is a bit ugly; this should be improved.
    uint256 hash256 = s.getSHA512Half ();
    uint128 ret (uint128::fromVoid (hash256.data()));

    s.secureErase ();

    return ret;
}

// --> seed
// <-- private root generator + public root generator
EC_KEY* CKey::GenerateRootDeterministicKey (const uint128& seed)
{
    BN_CTX* ctx = BN_CTX_new ();

    if (!ctx) return nullptr;

    EC_KEY* pkey = EC_KEY_new_by_curve_name (NID_secp256k1);

    if (!pkey)
    {
        BN_CTX_free (ctx);
        return nullptr;
    }

    EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);

    BIGNUM* order = BN_new ();

    if (!order)
    {
        BN_CTX_free (ctx);
        EC_KEY_free (pkey);
        return nullptr;
    }

    if (!EC_GROUP_get_order (EC_KEY_get0_group (pkey), order, ctx))
    {
        assert (false);
        BN_free (order);
        EC_KEY_free (pkey);
        BN_CTX_free (ctx);
        return nullptr;
    }

    BIGNUM* privKey = nullptr;
    int seq = 0;

    do
    {
        // private key must be non-zero and less than the curve's order
        Serializer s ((128 + 32) / 8);
        s.add128 (seed);
        s.add32 (seq++);
        uint256 root = s.getSHA512Half ();
        s.secureErase ();
        privKey = BN_bin2bn ((const unsigned char*) &root, sizeof (root), privKey);

        if (privKey == nullptr)
        {
            EC_KEY_free (pkey);
            BN_free (order);
            BN_CTX_free (ctx);
        }

        root.zero ();
    }
    while (BN_is_zero (privKey) || (BN_cmp (privKey, order) >= 0));

    BN_free (order);

    if (!EC_KEY_set_private_key (pkey, privKey))
    {
        // set the random point as the private key
        assert (false);
        EC_KEY_free (pkey);
        BN_clear_free (privKey);
        BN_CTX_free (ctx);
        return nullptr;
    }

    EC_POINT* pubKey = EC_POINT_new (EC_KEY_get0_group (pkey));

    if (!EC_POINT_mul (EC_KEY_get0_group (pkey), pubKey, privKey, nullptr, nullptr, ctx))
    {
        // compute the corresponding public key point
        assert (false);
        BN_clear_free (privKey);
        EC_POINT_free (pubKey);
        EC_KEY_free (pkey);
        BN_CTX_free (ctx);
        return nullptr;
    }

    BN_clear_free (privKey);

    if (!EC_KEY_set_public_key (pkey, pubKey))
    {
        assert (false);
        EC_POINT_free (pubKey);
        EC_KEY_free (pkey);
        BN_CTX_free (ctx);
        return nullptr;
    }

    EC_POINT_free (pubKey);

    BN_CTX_free (ctx);

#ifdef EC_DEBUG
    assert (EC_KEY_check_key (pkey) == 1); // CAUTION: This check is *very* expensive
#endif
    return pkey;
}

// Take ripple address.
// --> root public generator (consumes)
// <-- root public generator in EC format
EC_KEY* CKey::GenerateRootPubKey (BIGNUM* pubGenerator)
{
    if (pubGenerator == nullptr)
    {
        assert (false);
        return nullptr;
    }

    EC_KEY* pkey = EC_KEY_new_by_curve_name (NID_secp256k1);

    if (!pkey)
    {
        BN_free (pubGenerator);
        return nullptr;
    }

    EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);

    EC_POINT* pubPoint = EC_POINT_bn2point (EC_KEY_get0_group (pkey), pubGenerator, nullptr, nullptr);
    BN_free (pubGenerator);

    if (!pubPoint)
    {
        assert (false);
        EC_KEY_free (pkey);
        return nullptr;
    }

    if (!EC_KEY_set_public_key (pkey, pubPoint))
    {
        assert (false);
        EC_POINT_free (pubPoint);
        EC_KEY_free (pkey);
        return nullptr;
    }

    EC_POINT_free (pubPoint);

    return pkey;
}

// --> public generator
static BIGNUM* makeHash (const RippleAddress& pubGen, int seq, BIGNUM* order)
{
    int subSeq = 0;
    BIGNUM* ret = nullptr;

    do
    {
        Serializer s ((33 * 8 + 32 + 32) / 8);
        s.addRaw (pubGen.getGenerator ());
        s.add32 (seq);
        s.add32 (subSeq++);
        uint256 root = s.getSHA512Half ();
        s.secureErase ();
        ret = BN_bin2bn ((const unsigned char*) &root, sizeof (root), ret);

        if (!ret) return nullptr;
    }
    while (BN_is_zero (ret) || (BN_cmp (ret, order) >= 0));

    return ret;
}

// --> public generator
EC_KEY* CKey::GeneratePublicDeterministicKey (const RippleAddress& pubGen, int seq)
{
    // publicKey(n) = rootPublicKey EC_POINT_+ Hash(pubHash|seq)*point
    BIGNUM* generator = BN_bin2bn (
        pubGen.getGenerator ().data (),
        pubGen.getGenerator ().size (),
        nullptr);

    if (generator == nullptr)
        return nullptr;

    EC_KEY*         rootKey     = CKey::GenerateRootPubKey (generator);
    const EC_POINT* rootPubKey  = EC_KEY_get0_public_key (rootKey);
    BN_CTX*         ctx         = BN_CTX_new ();
    EC_KEY*         pkey        = EC_KEY_new_by_curve_name (NID_secp256k1);
    EC_POINT*       newPoint    = 0;
    BIGNUM*         order       = 0;
    BIGNUM*         hash        = 0;
    bool            success     = true;

    if (!ctx || !pkey)  success = false;

    if (success)
        EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);

    if (success)
    {
        newPoint    = EC_POINT_new (EC_KEY_get0_group (pkey));

        if (!newPoint)   success = false;
    }

    if (success)
    {
        order       = BN_new ();

        if (!order || !EC_GROUP_get_order (EC_KEY_get0_group (pkey), order, ctx))
            success = false;
    }

    // Calculate the private additional key.
    if (success)
    {
        hash        = makeHash (pubGen, seq, order);

        if (!hash)   success = false;
    }

    if (success)
    {
        // Calculate the corresponding public key.
        EC_POINT_mul (EC_KEY_get0_group (pkey), newPoint, hash, nullptr, nullptr, ctx);

        // Add the master public key and set.
        EC_POINT_add (EC_KEY_get0_group (pkey), newPoint, newPoint, rootPubKey, ctx);
        EC_KEY_set_public_key (pkey, newPoint);
    }

    if (order)              BN_free (order);

    if (hash)               BN_free (hash);

    if (newPoint)           EC_POINT_free (newPoint);

    if (ctx)                BN_CTX_free (ctx);

    if (rootKey)            EC_KEY_free (rootKey);

    if (pkey && !success)   EC_KEY_free (pkey);

    return success ? pkey : nullptr;
}

EC_KEY* CKey::GeneratePrivateDeterministicKey (const RippleAddress& pubGen, uint256 const& u, int seq)
{
    CBigNum bn (u);
    return GeneratePrivateDeterministicKey (pubGen, static_cast<BIGNUM*> (&bn), seq);
}

// --> root private key
EC_KEY* CKey::GeneratePrivateDeterministicKey (const RippleAddress& pubGen, const BIGNUM* rootPrivKey, int seq)
{
    // privateKey(n) = (rootPrivateKey + Hash(pubHash|seq)) % order
    BN_CTX* ctx = BN_CTX_new ();

    if (ctx == nullptr) return nullptr;

    EC_KEY* pkey = EC_KEY_new_by_curve_name (NID_secp256k1);

    if (pkey == nullptr)
    {
        BN_CTX_free (ctx);
        return nullptr;
    }

    EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);

    BIGNUM* order = BN_new ();

    if (order == nullptr)
    {
        BN_CTX_free (ctx);
        EC_KEY_free (pkey);
        return nullptr;
    }

    if (!EC_GROUP_get_order (EC_KEY_get0_group (pkey), order, ctx))
    {
        BN_free (order);
        BN_CTX_free (ctx);
        EC_KEY_free (pkey);
        return nullptr;
    }

    // calculate the private additional key
    BIGNUM* privKey = makeHash (pubGen, seq, order);

    if (privKey == nullptr)
    {
        BN_free (order);
        BN_CTX_free (ctx);
        EC_KEY_free (pkey);
        return nullptr;
    }

    // calculate the final private key
    BN_mod_add (privKey, privKey, rootPrivKey, order, ctx);
    BN_free (order);
    EC_KEY_set_private_key (pkey, privKey);

    // compute the corresponding public key
    EC_POINT* pubKey = EC_POINT_new (EC_KEY_get0_group (pkey));

    if (!pubKey)
    {
        BN_clear_free (privKey);
        BN_CTX_free (ctx);
        EC_KEY_free (pkey);
        return nullptr;
    }

    if (EC_POINT_mul (EC_KEY_get0_group (pkey), pubKey, privKey, nullptr, nullptr, ctx) == 0)
    {
        BN_clear_free (privKey);
        EC_POINT_free (pubKey);
        EC_KEY_free (pkey);
        BN_CTX_free (ctx);
        return nullptr;
    }

    BN_clear_free (privKey);
    EC_KEY_set_public_key (pkey, pubKey);

    EC_POINT_free (pubKey);
    BN_CTX_free (ctx);

    return pkey;
}

} // ripple
