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

// ECIES uses elliptic curve keys to send an encrypted message.

// A shared secret is generated from one public key and one private key.
// The same key results regardless of which key is public and which private.

// Anonymous messages can be sent by generating an ephemeral public/private
// key pair, using that private key with the recipient's public key to
// encrypt and publishing the ephemeral public key. Non-anonymous messages
// can be sent by using your own private key with the recipient's public key.

// A random IV is used to encrypt the message and an HMAC is used to ensure
// message integrity. If you need timestamps or need to tell the recipient
// which key to use (his, yours, or ephemeral) you must add that data.
// (Obviously, key information can't go in the encrypted portion anyway.)

// Our ciphertext is all encrypted except the IV. The encrypted data decodes as follows:
// 1) IV (unencrypted)
// 2) Encrypted: HMAC of original plaintext
// 3) Encrypted: Original plaintext
// 4) Encrypted: Rest of block/padding

// ECIES operations throw on any error such as a corrupt message or incorrect
// key. They *must* be called in try/catch blocks.

// Algorithmic choices:
#define ECIES_KEY_HASH      SHA512              // Hash used to expand shared secret
#define ECIES_KEY_LENGTH    (512/8)             // Size of expanded shared secret
#define ECIES_MIN_SEC       (128/8)             // The minimum equivalent security
#define ECIES_ENC_ALGO      EVP_aes_256_cbc()   // Encryption algorithm
#define ECIES_ENC_KEY_TYPE  uint256             // Type used to hold shared secret
#define ECIES_ENC_KEY_SIZE  (256/8)             // Encryption key size
#define ECIES_ENC_BLK_SIZE  (128/8)             // Encryption block size
#define ECIES_ENC_IV_TYPE   uint128             // Type used to hold IV
#define ECIES_HMAC_ALGO     EVP_sha256()        // HMAC algorithm
#define ECIES_HMAC_KEY_TYPE uint256             // Type used to hold HMAC key
#define ECIES_HMAC_KEY_SIZE (256/8)             // Size of HMAC key
#define ECIES_HMAC_TYPE     uint256             // Type used to hold HMAC value
#define ECIES_HMAC_SIZE     (256/8)             // Size of HMAC value

void CKey::getECIESSecret (CKey& otherKey, ECIES_ENC_KEY_TYPE& enc_key, ECIES_HMAC_KEY_TYPE& hmac_key)
{
    // Retrieve a secret generated from an EC key pair. At least one private key must be known.
    if (!pkey || !otherKey.pkey)
        throw std::runtime_error ("missing key");

    EC_KEY* pubkey, *privkey;

    if (EC_KEY_get0_private_key (pkey))
    {
        privkey = pkey;
        pubkey = otherKey.pkey;
    }
    else if (EC_KEY_get0_private_key (otherKey.pkey))
    {
        privkey = otherKey.pkey;
        pubkey = pkey;
    }
    else throw std::runtime_error ("no private key");

    unsigned char rawbuf[512];
    int buflen = ECDH_compute_key (rawbuf, 512, EC_KEY_get0_public_key (pubkey), privkey, nullptr);

    if (buflen < ECIES_MIN_SEC)
        throw std::runtime_error ("ecdh key failed");

    unsigned char hbuf[ECIES_KEY_LENGTH];
    ECIES_KEY_HASH (rawbuf, buflen, hbuf);
    memset (rawbuf, 0, ECIES_HMAC_KEY_SIZE);

    assert ((ECIES_ENC_KEY_SIZE + ECIES_HMAC_KEY_SIZE) >= ECIES_KEY_LENGTH);
    memcpy (enc_key.begin (), hbuf, ECIES_ENC_KEY_SIZE);
    memcpy (hmac_key.begin (), hbuf + ECIES_ENC_KEY_SIZE, ECIES_HMAC_KEY_SIZE);
    memset (hbuf, 0, ECIES_KEY_LENGTH);
}

static ECIES_HMAC_TYPE makeHMAC (const ECIES_HMAC_KEY_TYPE& secret, Blob const& data)
{
    HMAC_CTX ctx;
    HMAC_CTX_init (&ctx);

    if (HMAC_Init_ex (&ctx, secret.begin (), ECIES_HMAC_KEY_SIZE, ECIES_HMAC_ALGO, nullptr) != 1)
    {
        HMAC_CTX_cleanup (&ctx);
        throw std::runtime_error ("init hmac");
    }

    if (HMAC_Update (&ctx, & (data.front ()), data.size ()) != 1)
    {
        HMAC_CTX_cleanup (&ctx);
        throw std::runtime_error ("update hmac");
    }

    ECIES_HMAC_TYPE ret;
    unsigned int ml = ECIES_HMAC_SIZE;

    if (HMAC_Final (&ctx, ret.begin (), &ml) != 1)
    {
        HMAC_CTX_cleanup (&ctx);
        throw std::runtime_error ("finalize hmac");
    }

    assert (ml == ECIES_HMAC_SIZE);
    HMAC_CTX_cleanup (&ctx);

    return ret;
}

Blob CKey::encryptECIES (CKey& otherKey, Blob const& plaintext)
{

    ECIES_ENC_IV_TYPE iv;
    RandomNumbers::getInstance ().fillBytes (iv.begin (), ECIES_ENC_BLK_SIZE);

    ECIES_ENC_KEY_TYPE secret;
    ECIES_HMAC_KEY_TYPE hmacKey;

    getECIESSecret (otherKey, secret, hmacKey);
    ECIES_HMAC_TYPE hmac = makeHMAC (hmacKey, plaintext);
    hmacKey.zero ();

    EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX_init (&ctx);

    if (EVP_EncryptInit_ex (&ctx, ECIES_ENC_ALGO, nullptr, secret.begin (), iv.begin ()) != 1)
    {
        EVP_CIPHER_CTX_cleanup (&ctx);
        secret.zero ();
        throw std::runtime_error ("init cipher ctx");
    }

    secret.zero ();

    Blob out (plaintext.size () + ECIES_HMAC_SIZE + ECIES_ENC_KEY_SIZE + ECIES_ENC_BLK_SIZE, 0);
    int len = 0, bytesWritten;

    // output IV
    memcpy (& (out.front ()), iv.begin (), ECIES_ENC_BLK_SIZE);
    len = ECIES_ENC_BLK_SIZE;

    // Encrypt/output HMAC
    bytesWritten = out.capacity () - len;
    assert (bytesWritten > 0);

    if (EVP_EncryptUpdate (&ctx, & (out.front ()) + len, &bytesWritten, hmac.begin (), ECIES_HMAC_SIZE) < 0)
    {
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("");
    }

    len += bytesWritten;

    // encrypt/output plaintext
    bytesWritten = out.capacity () - len;
    assert (bytesWritten > 0);

    if (EVP_EncryptUpdate (&ctx, & (out.front ()) + len, &bytesWritten, & (plaintext.front ()), plaintext.size ()) < 0)
    {
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("");
    }

    len += bytesWritten;

    // finalize
    bytesWritten = out.capacity () - len;

    if (EVP_EncryptFinal_ex (&ctx, & (out.front ()) + len, &bytesWritten) < 0)
    {
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("encryption error");
    }

    len += bytesWritten;

    // Output contains: IV, encrypted HMAC, encrypted data, encrypted padding
    assert (len <= (plaintext.size () + ECIES_HMAC_SIZE + (2 * ECIES_ENC_BLK_SIZE)));
    assert (len >= (plaintext.size () + ECIES_HMAC_SIZE + ECIES_ENC_BLK_SIZE)); // IV, HMAC, data
    out.resize (len);
    EVP_CIPHER_CTX_cleanup (&ctx);
    return out;
}

Blob CKey::decryptECIES (CKey& otherKey, Blob const& ciphertext)
{
    // minimum ciphertext = IV + HMAC + 1 block
    if (ciphertext.size () < ((2 * ECIES_ENC_BLK_SIZE) + ECIES_HMAC_SIZE) )
        throw std::runtime_error ("ciphertext too short");

    // extract IV
    ECIES_ENC_IV_TYPE iv;
    memcpy (iv.begin (), & (ciphertext.front ()), ECIES_ENC_BLK_SIZE);

    // begin decrypting
    EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX_init (&ctx);

    ECIES_ENC_KEY_TYPE secret;
    ECIES_HMAC_KEY_TYPE hmacKey;
    getECIESSecret (otherKey, secret, hmacKey);

    if (EVP_DecryptInit_ex (&ctx, ECIES_ENC_ALGO, nullptr, secret.begin (), iv.begin ()) != 1)
    {
        secret.zero ();
        hmacKey.zero ();
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("unable to init cipher");
    }

    // decrypt mac
    ECIES_HMAC_TYPE hmac;
    int outlen = ECIES_HMAC_SIZE;

    if ( (EVP_DecryptUpdate (&ctx, hmac.begin (), &outlen,
                             & (ciphertext.front ()) + ECIES_ENC_BLK_SIZE, ECIES_HMAC_SIZE + 1) != 1) || (outlen != ECIES_HMAC_SIZE) )
    {
        secret.zero ();
        hmacKey.zero ();
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("unable to extract hmac");
    }

    // decrypt plaintext (after IV and encrypted mac)
    Blob plaintext (ciphertext.size () - ECIES_HMAC_SIZE - ECIES_ENC_BLK_SIZE);
    outlen = plaintext.size ();

    if (EVP_DecryptUpdate (&ctx, & (plaintext.front ()), &outlen,
                           & (ciphertext.front ()) + ECIES_ENC_BLK_SIZE + ECIES_HMAC_SIZE + 1,
                           ciphertext.size () - ECIES_ENC_BLK_SIZE - ECIES_HMAC_SIZE - 1) != 1)
    {
        secret.zero ();
        hmacKey.zero ();
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("unable to extract plaintext");
    }

    // decrypt padding
    int flen = 0;

    if (EVP_DecryptFinal (&ctx, & (plaintext.front ()) + outlen, &flen) != 1)
    {
        secret.zero ();
        hmacKey.zero ();
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("plaintext had bad padding");
    }

    plaintext.resize (flen + outlen);

    // verify integrity
    if (hmac != makeHMAC (hmacKey, plaintext))
    {
        secret.zero ();
        hmacKey.zero ();
        EVP_CIPHER_CTX_cleanup (&ctx);
        throw std::runtime_error ("plaintext had bad hmac");
    }

    secret.zero ();
    hmacKey.zero ();

    EVP_CIPHER_CTX_cleanup (&ctx);
    return plaintext;
}

bool checkECIES (void)
{
    CKey senderPriv, recipientPriv, senderPub, recipientPub;

    for (int i = 0; i < 30000; ++i)
    {
        if ((i % 100) == 0)
        {
            // generate new keys every 100 times
            //          Log::out() << "new keys";
            senderPriv.MakeNewKey ();
            recipientPriv.MakeNewKey ();

            if (!senderPub.SetPubKey (senderPriv.GetPubKey ()))
                throw std::runtime_error ("key error");

            if (!recipientPub.SetPubKey (recipientPriv.GetPubKey ()))
                throw std::runtime_error ("key error");
        }

        // generate message
        Blob message (4096);
        int msglen = i % 3000;

        RandomNumbers::getInstance ().fillBytes (&message.front (), msglen);
        message.resize (msglen);

        // encrypt message with sender's private key and recipient's public key
        Blob ciphertext = senderPriv.encryptECIES (recipientPub, message);

        // decrypt message with recipient's private key and sender's public key
        Blob decrypt = recipientPriv.decryptECIES (senderPub, ciphertext);

        if (decrypt != message)
        {
            assert (false);
            return false;
        }

        //Log::out() << "Msg(" << msglen << ") ok " << ciphertext.size();
    }

    return true;
}

} // ripple
