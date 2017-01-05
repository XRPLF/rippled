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
#include <ripple/crypto/csprng.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/rngfill.h>
#include <algorithm>
#include <string>
#include <vector>

namespace ripple {

class SecretKey_test : public beast::unit_test::suite
{
public:
    void testDigestSigning()
    {
        testcase ("secp256k1 digest");

        for (std::size_t i = 0; i < 32; i++)
        {
            auto const keypair = randomKeyPair (KeyType::secp256k1);

            BEAST_EXPECT(keypair.first == derivePublicKey (KeyType::secp256k1, keypair.second));
            BEAST_EXPECT(*publicKeyType (keypair.first) == KeyType::secp256k1);

            for (std::size_t j = 0; j < 32; j++)
            {
                uint256 digest;
                beast::rngfill (
                    digest.data(),
                    digest.size(),
                    crypto_prng());

                auto sig = signDigest (
                    keypair.first, keypair.second, digest);

                BEAST_EXPECT(sig.size() != 0);
                BEAST_EXPECT(verifyDigest (keypair.first,
                    digest, sig, true));

                // Wrong digest:
                BEAST_EXPECT(!verifyDigest (keypair.first,
                    ~digest, sig, true));

                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[j % sig.size()]++;

                // Wrong signature:
                BEAST_EXPECT(!verifyDigest (keypair.first,
                    digest, sig, true));

                // Wrong digest and signature:
                BEAST_EXPECT(!verifyDigest (keypair.first,
                    ~digest, sig, true));
            }
        }
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
                TOKEN_NODE_PRIVATE,
                "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe");
            BEAST_EXPECT(sk2);

            BEAST_EXPECT(sk1 == *sk2);
        }

        {
            auto const sk1 = generateSecretKey (
                KeyType::ed25519,
                generateSeed ("masterpassphrase"));

            auto const sk2 = parseBase58<SecretKey> (
                TOKEN_NODE_PRIVATE,
                "paKv46LztLqK3GaKz1rG2nQGN6M4JLyRtxFBYFTw4wAVHtGys36");
            BEAST_EXPECT(sk2);

            BEAST_EXPECT(sk1 == *sk2);
        }

        // Try converting short, long and malformed data
        BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, ""));
        BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, " "));
        BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, "!35gty9mhju8nfjl"));

        auto const good = toBase58 (
            TokenType::TOKEN_NODE_PRIVATE,
            randomSecretKey());

        // Short (non-empty) strings
        {
            auto s = good;

            // Remove all characters from the string in random order:
            std::hash<std::string> r;

            while (!s.empty())
            {
                s.erase (r(s) % s.size(), 1);
                BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, s));
            }
        }

        // Long strings
        for (std::size_t i = 1; i != 16; i++)
        {
            auto s = good;
            s.resize (s.size() + i, s[i % s.size()]);
            BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, s));
        }

        // Strings with invalid Base58 characters
        for (auto c : std::string ("0IOl"))
        {
            for (std::size_t i = 0; i != good.size(); ++i)
            {
                auto s = good;
                s[i % s.size()] = c;
                BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, s));
            }
        }

        // Strings with incorrect prefix
        {
            auto s = good;

            for (auto c : std::string("ansrJqtv7"))
            {
                s[0] = c;
                BEAST_EXPECT(!parseBase58<SecretKey> (TOKEN_NODE_PRIVATE, s));
            }
        }

        // Try some random secret keys
        std::array <SecretKey, 32> keys;

        for (std::size_t i = 0; i != keys.size(); ++i)
            keys[i] = randomSecretKey();

        for (std::size_t i = 0; i != keys.size(); ++i)
        {
            auto const si = toBase58 (
                TokenType::TOKEN_NODE_PRIVATE,
                keys[i]);
            BEAST_EXPECT(!si.empty());

            auto const ski = parseBase58<SecretKey> (
                TOKEN_NODE_PRIVATE, si);
            BEAST_EXPECT(ski && keys[i] == *ski);

            for (std::size_t j = i; j != keys.size(); ++j)
            {
                BEAST_EXPECT((keys[i] == keys[j]) == (i == j));

                auto const sj = toBase58 (
                    TokenType::TOKEN_NODE_PRIVATE,
                    keys[j]);

                BEAST_EXPECT((si == sj) == (i == j));

                auto const skj = parseBase58<SecretKey> (
                    TOKEN_NODE_PRIVATE, sj);
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
        testDigestSigning();
        testMiscOperations();

        testcase ("secp256k1");
        testSigning(KeyType::secp256k1);

        testcase ("ed25519");
        testSigning(KeyType::ed25519);
    }
};

BEAST_DEFINE_TESTSUITE(SecretKey,protocol,ripple);

} // ripple
