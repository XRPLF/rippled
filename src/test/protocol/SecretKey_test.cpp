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

#include <ripple/crypto/csprng.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/rngfill.h>
#include <algorithm>
#include <string>
#include <vector>

#include <ripple/protocol/impl/secp256k1.h>

namespace ripple {

class SecretKey_test : public beast::unit_test::suite
{
public:
    using blob = std::vector<std::uint8_t>;

    template <class FwdIter, class Container>
    static
    void
    hex_to_binary (FwdIter first, FwdIter last, Container& out)
    {
        struct Table
        {
            int val[256];
            Table ()
            {
                std::fill (val, val+256, 0);
                for (int i = 0; i < 10; ++i)
                    val ['0'+i] = i;
                for (int i = 0; i < 6; ++i)
                {
                    val ['A'+i] = 10 + i;
                    val ['a'+i] = 10 + i;
                }
            }
            int operator[] (int i)
            {
               return val[i];
            }
        };

        static Table lut;
        out.reserve (std::distance (first, last) / 2);
        while (first != last)
        {
            auto const hi (lut[(*first++)]);
            auto const lo (lut[(*first++)]);
            out.push_back ((hi*16)+lo);
        }
    }

    static
    Blob
    hex_to_digest(std::string const& s)
    {
        blob b;
        hex_to_binary (s.begin (), s.end (), b);
        return b;
    }

    static
    PublicKey
    hex_to_pk(std::string const& s)
    {
        blob b;
        hex_to_binary (s.begin (), s.end (), b);
        return PublicKey{Slice{b.data(), b.size()}};
    }

    static
    SecretKey
    hex_to_sk(std::string const& s)
    {
        blob b;
        hex_to_binary (s.begin (), s.end (), b);
        return SecretKey{Slice{b.data(), b.size()}};
    }

    static
    Buffer
    hex_to_sig(std::string const& s)
    {
        blob b;
        hex_to_binary (s.begin (), s.end (), b);
        return Buffer{Slice{b.data(), b.size()}};
    }

    // VFALCO We can remove this commented out code
    //        later, when we have confidence in the vectors.

    /*
    Buffer
    makeNonCanonical(Buffer const& sig)
    {
        secp256k1_ecdsa_signature sigin;
        BEAST_EXPECT(secp256k1_ecdsa_signature_parse_der(
            secp256k1Context(),
            &sigin,
            reinterpret_cast<unsigned char const*>(
                sig.data()),
            sig.size()) == 1);
        secp256k1_ecdsa_signature sigout;
        BEAST_EXPECT(secp256k1_ecdsa_signature_denormalize(
            secp256k1Context(),
            &sigout,
            &sigin) == 1);
        unsigned char buf[72];
        size_t len = sizeof(buf);
        BEAST_EXPECT(secp256k1_ecdsa_signature_serialize_der(
            secp256k1Context(),
            buf,
            &len,
            &sigout) == 1);
        return Buffer{buf, len};
    }

    void
    makeCanonicalityTestVectors()
    {
        uint256 digest;
        beast::rngfill (
            digest.data(),
            digest.size(),
            crypto_prng());
        log << "digest " << strHex(digest.data(), digest.size()) << std::endl;

        auto const sk = randomSecretKey();
        auto const pk = derivePublicKey(KeyType::secp256k1, sk);
        log << "public " << pk << std::endl;
        log << "secret " << sk.to_string() << std::endl;

        auto sig = signDigest(pk, sk, digest);
        log << "canonical sig " << strHex(sig) << std::endl;

        auto const non = makeNonCanonical(sig);
        log << "non-canon sig " << strHex(non) << std::endl;

        {
            auto const canonicality = ecdsaCanonicality(sig);
            BEAST_EXPECT(canonicality);
            BEAST_EXPECT(*canonicality == ECDSACanonicality::fullyCanonical);
        }

        {
            auto const canonicality = ecdsaCanonicality(non);
            BEAST_EXPECT(canonicality);
            BEAST_EXPECT(*canonicality != ECDSACanonicality::fullyCanonical);
        }

        BEAST_EXPECT(verifyDigest(pk, digest, sig, false));
        BEAST_EXPECT(verifyDigest(pk, digest, sig, true));
        BEAST_EXPECT(verifyDigest(pk, digest, non, false));
        BEAST_EXPECT(! verifyDigest(pk, digest, non, true));
    }
    */

    // Ensure that verification does the right thing with
    // respect to the matrix of canonicality variables.
    void
    testCanonicality()
    {
        testcase ("secp256k1 canonicality");

#if 0
        makeCanonicalityTestVectors();
#else
        auto const digest = hex_to_digest("34C19028C80D21F3F48C9354895F8D5BF0D5EE7FF457647CF655F5530A3022A7");
        auto const pk = hex_to_pk("025096EB12D3E924234E7162369C11D8BF877EDA238778E7A31FF0AAC5D0DBCF37");
        auto const sk = hex_to_sk("AA921417E7E5C299DA4EEC16D1CAA92F19B19F2A68511F68EC73BBB2F5236F3D");
        auto const sig = hex_to_sig("3045022100C2EC8B76743C718241ABB81BDA4434C97FE62E1EC27B40A1BA42D3344EF59CBD022029E9722F18B302DBDB0D573CED8EB26094667F03ACEF0239B0AA712B525A93A6");
        auto const non = hex_to_sig("3046022100C2EC8B76743C718241ABB81BDA4434C97FE62E1EC27B40A1BA42D3344EF59CBD022100D6168DD0E74CFD2424F2A8C312714D9E26485DE302599E020F27ED617DDBAD9B");

        {
            auto const canonicality = ecdsaCanonicality(sig);
            BEAST_EXPECT(canonicality);
            BEAST_EXPECT(*canonicality == ECDSACanonicality::fullyCanonical);
        }

        {
            auto const canonicality = ecdsaCanonicality(non);
            BEAST_EXPECT(canonicality);
            BEAST_EXPECT(*canonicality != ECDSACanonicality::fullyCanonical);
        }

        BEAST_EXPECT(verify(pk, makeSlice(digest), sig, false));
        BEAST_EXPECT(verify(pk, makeSlice(digest), sig, true));
        BEAST_EXPECT(verify(pk, makeSlice(digest), non, false));
        BEAST_EXPECT(! verify(pk, makeSlice(digest), non, true));
#endif
    }

    void testSigning (KeyType type)
    {
        for (std::size_t i = 0; i < 32; i++)
        {
            auto const keypair = randomKeyPair (type);

            BEAST_EXPECT(keypair.first == derivePublicKey (type, keypair.second));
            BEAST_EXPECT(*publicKeyType (keypair.first) == type);

            for (std::size_t j = 0; j < 32; j++)
            {
                std::vector<std::uint8_t> data (64 + (8 * i) + j);
                beast::rngfill (
                    data.data(),
                    data.size(),
                    crypto_prng());

                auto sig = sign (
                    keypair.first, keypair.second,
                    makeSlice (data));

                BEAST_EXPECT(sig.size() != 0);
                BEAST_EXPECT(verify(keypair.first,
                    makeSlice(data), sig, true));

                // Construct wrong data:
                auto badData = data;

                // swaps the smallest and largest elements in buffer
                std::iter_swap (
                    std::min_element (badData.begin(), badData.end()),
                    std::max_element (badData.begin(), badData.end()));

                // Wrong data: should fail
                BEAST_EXPECT(!verify (keypair.first,
                    makeSlice(badData), sig, true));

                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[j % sig.size()]++;

                // Wrong signature: should fail
                BEAST_EXPECT(!verify (keypair.first,
                    makeSlice(data), sig, true));

                // Wrong data and signature: should fail
                BEAST_EXPECT(!verify (keypair.first,
                    makeSlice(badData), sig, true));
            }
        }
    }

    void testBase58 ()
    {
        testcase ("Base58");

        // Ensure that parsing some well-known secret keys works
        {
            auto const sk1 = generateSecretKey (
                KeyType::secp256k1,
                generateSeed ("masterpassphrase"));

            auto const sk2 = parseBase58<SecretKey> (
                TokenType::NodePrivate,
                "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe");
            BEAST_EXPECT(sk2);

            BEAST_EXPECT(sk1 == *sk2);
        }

        {
            auto const sk1 = generateSecretKey (
                KeyType::ed25519,
                generateSeed ("masterpassphrase"));

            auto const sk2 = parseBase58<SecretKey> (
                TokenType::NodePrivate,
                "paKv46LztLqK3GaKz1rG2nQGN6M4JLyRtxFBYFTw4wAVHtGys36");
            BEAST_EXPECT(sk2);

            BEAST_EXPECT(sk1 == *sk2);
        }

        // Try converting short, long and malformed data
        BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, ""));
        BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, " "));
        BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, "!35gty9mhju8nfjl"));

        auto const good = toBase58 (
            TokenType::NodePrivate,
            randomSecretKey());

        // Short (non-empty) strings
        {
            auto s = good;

            // Remove all characters from the string in random order:
            std::hash<std::string> r;

            while (!s.empty())
            {
                s.erase (r(s) % s.size(), 1);
                BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, s));
            }
        }

        // Long strings
        for (std::size_t i = 1; i != 16; i++)
        {
            auto s = good;
            s.resize (s.size() + i, s[i % s.size()]);
            BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, s));
        }

        // Strings with invalid Base58 characters
        for (auto c : std::string ("0IOl"))
        {
            for (std::size_t i = 0; i != good.size(); ++i)
            {
                auto s = good;
                s[i % s.size()] = c;
                BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, s));
            }
        }

        // Strings with incorrect prefix
        {
            auto s = good;

            for (auto c : std::string("ansrJqtv7"))
            {
                s[0] = c;
                BEAST_EXPECT(!parseBase58<SecretKey> (TokenType::NodePrivate, s));
            }
        }

        // Try some random secret keys
        std::array <SecretKey, 32> keys;

        for (std::size_t i = 0; i != keys.size(); ++i)
            keys[i] = randomSecretKey();

        for (std::size_t i = 0; i != keys.size(); ++i)
        {
            auto const si = toBase58 (
                TokenType::NodePrivate,
                keys[i]);
            BEAST_EXPECT(!si.empty());

            auto const ski = parseBase58<SecretKey> (
                TokenType::NodePrivate, si);
            BEAST_EXPECT(ski && keys[i] == *ski);

            for (std::size_t j = i; j != keys.size(); ++j)
            {
                BEAST_EXPECT((keys[i] == keys[j]) == (i == j));

                auto const sj = toBase58 (
                    TokenType::NodePrivate,
                    keys[j]);

                BEAST_EXPECT((si == sj) == (i == j));

                auto const skj = parseBase58<SecretKey> (
                    TokenType::NodePrivate, sj);
                BEAST_EXPECT(skj && keys[j] == *skj);

                BEAST_EXPECT((*ski == *skj) == (i == j));
            }
        }
    }

    void testMiscOperations ()
    {
        testcase ("Miscellaneous operations");

        auto const sk1 = generateSecretKey (
            KeyType::secp256k1,
            generateSeed ("masterpassphrase"));

        SecretKey sk2 (sk1);
        BEAST_EXPECT(sk1 == sk2);

        SecretKey sk3;
        BEAST_EXPECT(sk3 != sk2);
        sk3 = sk2;
        BEAST_EXPECT(sk3 == sk2);
    }

    void run() override
    {
        testBase58();
        testMiscOperations();
        testCanonicality();

        testcase ("secp256k1");
        testSigning(KeyType::secp256k1);

        testcase ("ed25519");
        testSigning(KeyType::ed25519);
    }
};

BEAST_DEFINE_TESTSUITE(SecretKey,protocol,ripple);

} // ripple
