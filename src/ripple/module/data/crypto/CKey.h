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

#ifndef RIPPLE_CKEY_H
#define RIPPLE_CKEY_H

namespace ripple {

// secp256k1:
// const unsigned int PRIVATE_KEY_SIZE = 279;
// const unsigned int PUBLIC_KEY_SIZE  = 65; // but we don't use full keys
// const unsigned int COMPUB_KEY_SIZE  = 33;
// const unsigned int SIGNATURE_SIZE   = 72;
//
// see www.keylength.com
// script supports up to 75 for single byte push

// VFALCO NOTE this is unused
/*
int static inline EC_KEY_regenerate_key(EC_KEY *eckey, BIGNUM *priv_key)
{
    int okay = 0;
    BN_CTX *ctx = NULL;
    EC_POINT *pub_key = NULL;

    if (!eckey) return 0;

    const EC_GROUP *group = EC_KEY_get0_group(eckey);

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;

    pub_key = EC_POINT_new(group);

    if (pub_key == NULL)
        goto err;

    if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx))
        goto err;

    EC_KEY_set_conv_form(eckey, POINT_CONVERSION_COMPRESSED);
    EC_KEY_set_private_key(eckey, priv_key);
    EC_KEY_set_public_key(eckey, pub_key);

    okay = 1;

err:

    if (pub_key)
        EC_POINT_free(pub_key);
    if (ctx != NULL)
        BN_CTX_free(ctx);

    return (okay);
}
*/

class key_error : public std::runtime_error
{
public:
    explicit key_error (const std::string& str) : std::runtime_error (str) {}
};

class CKey
{
protected:
    EC_KEY* pkey;
    bool fSet;


public:
    typedef std::shared_ptr<CKey> pointer;

    CKey ()
    {
        pkey = EC_KEY_new_by_curve_name (NID_secp256k1);

        if (pkey == nullptr)
            throw key_error ("CKey::CKey() : EC_KEY_new_by_curve_name failed");

        EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);

        fSet = false;
    }

    CKey (const CKey& b)
    {
        pkey = EC_KEY_dup (b.pkey);

        if (pkey == nullptr)
            throw key_error ("CKey::CKey(const CKey&) : EC_KEY_dup failed");

        EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);

        fSet = b.fSet;
    }

    CKey& operator= (const CKey& b)
    {
        if (!EC_KEY_copy (pkey, b.pkey))
            throw key_error ("CKey::operator=(const CKey&) : EC_KEY_copy failed");

        fSet = b.fSet;
        return (*this);
    }


    ~CKey ()
    {
        EC_KEY_free (pkey);
    }


    static uint128 PassPhraseToKey (const std::string& passPhrase);
    static EC_KEY* GenerateRootDeterministicKey (const uint128& passPhrase);
    static EC_KEY* GenerateRootPubKey (BIGNUM* pubGenerator);
    static EC_KEY* GeneratePublicDeterministicKey (const RippleAddress& generator, int n);
    static EC_KEY* GeneratePrivateDeterministicKey (const RippleAddress& family, const BIGNUM* rootPriv, int n);
    static EC_KEY* GeneratePrivateDeterministicKey (const RippleAddress& family, uint256 const& rootPriv, int n);

    CKey (const uint128& passPhrase) : fSet (false)
    {
        pkey = GenerateRootDeterministicKey (passPhrase);
        fSet = true;
        assert (pkey);
    }

    CKey (const RippleAddress& generator, int n) : fSet (false)
    {
        // public deterministic key
        pkey = GeneratePublicDeterministicKey (generator, n);
        fSet = true;
        assert (pkey);
    }

    CKey (const RippleAddress& base, const BIGNUM* rootPrivKey, int n) : fSet (false)
    {
        // private deterministic key
        pkey = GeneratePrivateDeterministicKey (base, rootPrivKey, n);
        fSet = true;
        assert (pkey);
    }

    CKey (uint256 const& privateKey) : pkey (nullptr), fSet (false)
    {
        // XXX Broken pkey is null.
        SetPrivateKeyU (privateKey);
    }

#if 0
    CKey (const RippleAddress& masterKey, int keyNum, bool isPublic) : pkey (nullptr), fSet (false)
    {
        if (isPublic)
            SetPubSeq (masterKey, keyNum);
        else
            SetPrivSeq (masterKey, keyNum); // broken, need seed

        fSet = true;
    }
#endif

    bool IsNull () const
    {
        return !fSet;
    }

    void MakeNewKey ()
    {
        if (!EC_KEY_generate_key (pkey))
            throw key_error ("CKey::MakeNewKey() : EC_KEY_generate_key failed");

        EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);
        fSet = true;
    }

    // XXX Still used!
    BIGNUM* GetSecretBN () const
    {
        // DEPRECATED
        return BN_dup (EC_KEY_get0_private_key (pkey));
    }

    void GetPrivateKeyU (uint256& privKey)
    {
        const BIGNUM* bn = EC_KEY_get0_private_key (pkey);

        if (bn == nullptr)
            throw key_error ("CKey::GetPrivateKeyU: EC_KEY_get0_private_key failed");

        privKey.zero ();
        BN_bn2bin (bn, privKey.begin () + (privKey.size () - BN_num_bytes (bn)));
    }

    bool SetPrivateKeyU (uint256 const& key, bool bThrow = false)
    {
        // XXX Broken if pkey is not set.
        BIGNUM* bn          = BN_bin2bn (key.begin (), key.size (), nullptr);
        bool    bSuccess    = !!EC_KEY_set_private_key (pkey, bn);

        BN_clear_free (bn);

        if (bSuccess)
        {
            fSet = true;
        }
        else if (bThrow)
        {
            throw key_error ("CKey::SetPrivateKeyU: EC_KEY_set_private_key failed");
        }

        return bSuccess;
    }

    bool SetPubKey (const void* ptr, size_t len)
    {
        const unsigned char* pbegin = static_cast<const unsigned char*> (ptr);

        if (!o2i_ECPublicKey (&pkey, &pbegin, len))
            return false;

        EC_KEY_set_conv_form (pkey, POINT_CONVERSION_COMPRESSED);
        fSet = true;
        return true;
    }

    bool SetPubKey (Blob const& vchPubKey)
    {
        return SetPubKey (&vchPubKey[0], vchPubKey.size ());
    }

    bool SetPubKey (const std::string& pubKey)
    {
        return SetPubKey (pubKey.data (), pubKey.size ());
    }

    Blob GetPubKey () const
    {
        unsigned int nSize = i2o_ECPublicKey (pkey, nullptr);
        assert (nSize <= 33);

        if (!nSize)
            throw key_error ("CKey::GetPubKey() : i2o_ECPublicKey failed");

        Blob vchPubKey (33, 0);
        unsigned char* pbegin = &vchPubKey[0];

        if (i2o_ECPublicKey (pkey, &pbegin) != nSize)
            throw key_error ("CKey::GetPubKey() : i2o_ECPublicKey returned unexpected size");

        assert (vchPubKey.size () <= 33);
        return vchPubKey;
    }

    bool Sign (uint256 const& hash, Blob& vchSig)
    {
        unsigned char pchSig[128];
        unsigned int nSize = sizeof(pchSig)/sizeof(pchSig[0]) - 1;

        if (!ECDSA_sign (0, (unsigned char*)hash.begin (), hash.size (), pchSig, &nSize, pkey))
            return false;

        size_t len = nSize;
        makeCanonicalECDSASig (pchSig, len);
        vchSig.resize (len);
        memcpy (&vchSig[0], pchSig, len);

        return true;
    }

    bool Verify (uint256 const& hash, const void* sig, size_t sigLen) const
    {
        // -1 = error, 0 = bad sig, 1 = good
        if (ECDSA_verify (0, hash.begin (), hash.size (), (const unsigned char*) sig, sigLen, pkey) != 1)
            return false;

        return true;
    }

    bool Verify (uint256 const& hash, Blob const& vchSig) const
    {
        return Verify (hash, &vchSig[0], vchSig.size ());
    }

    bool Verify (uint256 const& hash, const std::string& sig) const
    {
        return Verify (hash, sig.data (), sig.size ());
    }

    // ECIES functions. These throw on failure

    // returns a 32-byte secret unique to these two keys. At least one private key must be known.
    void getECIESSecret (CKey& otherKey, uint256& enc_key, uint256& hmac_key);

    // encrypt/decrypt functions with integrity checking.
    // Note that the other side must somehow know what keys to use
    Blob encryptECIES (CKey& otherKey, Blob const& plaintext);
    Blob decryptECIES (CKey& otherKey, Blob const& ciphertext);
};

} // ripple

#endif
