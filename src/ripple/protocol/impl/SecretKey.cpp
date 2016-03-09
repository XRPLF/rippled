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
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/impl/secp256k1.h>
#include <ripple/basics/contract.h>
#include <ripple/crypto/GenerateDeterministicKey.h>
#include <ripple/crypto/csprng.h>
#include <beast/crypto/secure_erase.h>
#include <beast/rngfill.h>
#include <ed25519-donna/ed25519.h>
#include <cstring>

namespace ripple {

SecretKey::~SecretKey()
{
    beast::secure_erase(buf_, sizeof(buf_));
}

SecretKey::SecretKey (Slice const& slice)
{
    if (slice.size() != sizeof(buf_))
        LogicError("SecretKey::SecretKey: invalid size");
    std::memcpy(buf_, slice.data(), sizeof(buf_));
}

//------------------------------------------------------------------------------

Generator::Generator (Seed const& seed)
{
    uint128 ui;
    std::memcpy(ui.data(),
        seed.data(), seed.size());
    gen_ = generateRootDeterministicPublicKey(ui);
}

std::pair<PublicKey, SecretKey>
Generator::operator()(Seed const& seed, std::size_t ordinal) const
{
    uint128 ui;
    std::memcpy(ui.data(), seed.data(), seed.size());
    auto gsk = generatePrivateDeterministicKey(gen_, ui, ordinal);
    auto gpk = generatePublicDeterministicKey(gen_, ordinal);
    SecretKey const sk(Slice{ gsk.data(), gsk.size() });
    PublicKey const pk(Slice{ gpk.data(), gpk.size() });
    beast::secure_erase(ui.data(), ui.size());
    beast::secure_erase(gsk.data(), gsk.size());
    return { pk, sk };
}

PublicKey
Generator::operator()(std::size_t ordinal) const
{
    auto gpk = generatePublicDeterministicKey(gen_, ordinal);
    return PublicKey(Slice{ gpk.data(), gpk.size() });
}

//------------------------------------------------------------------------------

Buffer
signDigest (PublicKey const& pk, SecretKey const& sk,
    uint256 const& digest)
{
    if (publicKeyType(pk.slice()) != KeyType::secp256k1)
        LogicError("sign: secp256k1 required for digest signing");

    int siglen = 72;
    unsigned char sig[72];
    auto const result = secp256k1_ecdsa_sign(
        secp256k1Context(),
            digest.data(), sig, &siglen,
                sk.data(), secp256k1_nonce_function_rfc6979,
                    nullptr);
    if (result != 1)
        LogicError("sign: secp256k1_ecdsa_sign failed");
    return Buffer(sig, siglen);
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
        int siglen = 72;
        unsigned char sig[72];
        auto const result = secp256k1_ecdsa_sign(
            secp256k1Context(),
                digest.data(), sig, &siglen,
                    sk.data(), secp256k1_nonce_function_rfc6979,
                        nullptr);
        if (result != 1)
            LogicError("sign: secp256k1_ecdsa_sign failed");
        return Buffer(sig, siglen);
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
        int len;
        unsigned char buf[33];
        auto const result =
            secp256k1_ec_pubkey_create(
                secp256k1Context(),
                    buf, &len, sk.data(), 1);
        if (result != 1)
            LogicError("derivePublicKey: failure");
        return PublicKey(Slice{ buf,
            static_cast<std::size_t>(len) });
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

