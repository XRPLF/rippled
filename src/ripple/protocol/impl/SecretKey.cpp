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

#include <BeastConfig.h>
#include <ripple/basics/strHex.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/impl/secp256k1.h>
#include <ripple/basics/contract.h>
#include <ripple/crypto/GenerateDeterministicKey.h>
#include <ripple/crypto/csprng.h>
#include <ripple/beast/crypto/secure_erase.h>
#include <ripple/beast/utility/rngfill.h>
#include <ed25519-donna/ed25519.h>
#include <cstring>

namespace ripple {

SecretKey::~SecretKey()
{
    beast::secure_erase(buf_, sizeof(buf_));
}

SecretKey::SecretKey (std::array<std::uint8_t, 32> const& key)
{
    std::memcpy(buf_, key.data(), key.size());
}

SecretKey::SecretKey (Slice const& slice)
{
    if (slice.size() != sizeof(buf_))
        LogicError("SecretKey::SecretKey: invalid size");
    std::memcpy(buf_, slice.data(), sizeof(buf_));
}

std::string
SecretKey::to_string() const
{
    return strHex(data(), size());
}

//------------------------------------------------------------------------------
/** Produces a sequence of secp256k1 key pairs. */
class Generator
{
private:
    Blob gen_; // VFALCO compile time size?

public:
    explicit
    Generator (Seed const& seed)
    {
        // FIXME: Avoid copying the seed into a uint128 key only to have
        //        generateRootDeterministicPublicKey copy out of it.
        uint128 ui;
        std::memcpy(ui.data(),
            seed.data(), seed.size());
        gen_ = generateRootDeterministicPublicKey(ui);
    }

    /** Generate the nth key pair.

        The seed is required to produce the private key.
    */
    std::pair<PublicKey, SecretKey>
    operator()(Seed const& seed, std::size_t ordinal) const
    {
        // FIXME: Avoid copying the seed into a uint128 key only to have
        //        generatePrivateDeterministicKey copy out of it.
        uint128 ui;
        std::memcpy(ui.data(), seed.data(), seed.size());
        auto gsk = generatePrivateDeterministicKey(gen_, ui, ordinal);
        auto gpk = generatePublicDeterministicKey(gen_, ordinal);
        SecretKey const sk(Slice
        { gsk.data(), gsk.size() });
        PublicKey const pk(Slice
        { gpk.data(), gpk.size() });
        beast::secure_erase(ui.data(), ui.size());
        beast::secure_erase(gsk.data(), gsk.size());
        return {pk, sk};
    }
};

//------------------------------------------------------------------------------

Buffer
signDigest (PublicKey const& pk, SecretKey const& sk,
    uint256 const& digest)
{
    if (publicKeyType(pk.slice()) != KeyType::secp256k1)
        LogicError("sign: secp256k1 required for digest signing");

    BOOST_ASSERT(sk.size() == 32);
    secp256k1_ecdsa_signature sig_imp;
    if(secp256k1_ecdsa_sign(
            secp256k1Context(),
            &sig_imp,
            reinterpret_cast<unsigned char const*>(
                digest.data()),
            reinterpret_cast<unsigned char const*>(
                sk.data()),
            secp256k1_nonce_function_rfc6979,
            nullptr) != 1)
        LogicError("sign: secp256k1_ecdsa_sign failed");

    unsigned char sig[72];
    size_t len = sizeof(sig);
    if(secp256k1_ecdsa_signature_serialize_der(
            secp256k1Context(),
            sig,
            &len,
            &sig_imp) != 1)
        LogicError("sign: secp256k1_ecdsa_signature_serialize_der failed");

    return Buffer{sig, len};
}

Buffer
sign (PublicKey const& pk,
    SecretKey const& sk, Slice const& m)
{
    auto const type =
        publicKeyType(pk.slice());
    if (! type)
        LogicError("sign: invalid type");
    switch(*type)
    {
    case KeyType::ed25519:
    {
        Buffer b(64);
        ed25519_sign(m.data(), m.size(),
            sk.data(), pk.data() + 1, b.data());
        return b;
    }
    case KeyType::secp256k1:
    {
        sha512_half_hasher h;
        h(m.data(), m.size());
        auto const digest =
            sha512_half_hasher::result_type(h);

        secp256k1_ecdsa_signature sig_imp;
        if(secp256k1_ecdsa_sign(
                secp256k1Context(),
                &sig_imp,
                reinterpret_cast<unsigned char const*>(
                    digest.data()),
                reinterpret_cast<unsigned char const*>(
                    sk.data()),
                secp256k1_nonce_function_rfc6979,
                nullptr) != 1)
            LogicError("sign: secp256k1_ecdsa_sign failed");

        unsigned char sig[72];
        size_t len = sizeof(sig);
        if(secp256k1_ecdsa_signature_serialize_der(
                secp256k1Context(),
                sig,
                &len,
                &sig_imp) != 1)
            LogicError("sign: secp256k1_ecdsa_signature_serialize_der failed");

        return Buffer{sig, len};
    }
    default:
        LogicError("sign: invalid type");
    }
}

SecretKey
randomSecretKey()
{
    std::uint8_t buf[32];
    beast::rngfill(
        buf,
        sizeof(buf),
        crypto_prng());
    SecretKey sk(Slice{ buf, sizeof(buf) });
    beast::secure_erase(buf, sizeof(buf));
    return sk;
}

// VFALCO TODO Rewrite all this without using OpenSSL
//             or calling into GenerateDetermisticKey
SecretKey
generateSecretKey (KeyType type, Seed const& seed)
{
    if (type == KeyType::ed25519)
    {
        auto const key = sha512Half_s(Slice(
            seed.data(), seed.size()));
        return SecretKey(Slice{ key.data(), key.size() });
    }

    if (type == KeyType::secp256k1)
    {
        // FIXME: Avoid copying the seed into a uint128 key only to have
        //        generateRootDeterministicPrivateKey copy out of it.
        uint128 ps;
        std::memcpy(ps.data(),
            seed.data(), seed.size());
        auto const upk =
            generateRootDeterministicPrivateKey(ps);
        return SecretKey(Slice{ upk.data(), upk.size() });
    }

    LogicError ("generateSecretKey: unknown key type");
}

PublicKey
derivePublicKey (KeyType type, SecretKey const& sk)
{
    switch(type)
    {
    case KeyType::secp256k1:
    {
        secp256k1_pubkey pubkey_imp;
        if(secp256k1_ec_pubkey_create(
                secp256k1Context(),
                &pubkey_imp,
                reinterpret_cast<unsigned char const*>(
                    sk.data())) != 1)
            LogicError("derivePublicKey: secp256k1_ec_pubkey_create failed");

        unsigned char pubkey[33];
        size_t len = sizeof(pubkey);
        if(secp256k1_ec_pubkey_serialize(
                secp256k1Context(),
                pubkey,
                &len,
                &pubkey_imp,
                SECP256K1_EC_COMPRESSED) != 1)
            LogicError("derivePublicKey: secp256k1_ec_pubkey_serialize failed");

        return PublicKey{Slice{pubkey,
            static_cast<std::size_t>(len)}};
    }
    case KeyType::ed25519:
    {
        unsigned char buf[33];
        buf[0] = 0xED;
        ed25519_publickey(sk.data(), &buf[1]);
        return PublicKey(Slice{ buf, sizeof(buf) });
    }
    default:
        LogicError("derivePublicKey: bad key type");
    };
}

std::pair<PublicKey, SecretKey>
generateKeyPair (KeyType type, Seed const& seed)
{
    switch(type)
    {
    case KeyType::secp256k1:
    {
        Generator g(seed);
        return g(seed, 0);
    }
    default:
    case KeyType::ed25519:
    {
        auto const sk = generateSecretKey(type, seed);
        return { derivePublicKey(type, sk), sk };
    }
    }
}

std::pair<PublicKey, SecretKey>
randomKeyPair (KeyType type)
{
    auto const sk = randomSecretKey();
    return { derivePublicKey(type, sk), sk };
}

template <>
boost::optional<SecretKey>
parseBase58 (TokenType type, std::string const& s)
{
    auto const result = decodeBase58Token(s, type);
    if (result.empty())
        return boost::none;
    if (result.size() != 32)
        return boost::none;
    return SecretKey(makeSlice(result));
}

} // ripple

