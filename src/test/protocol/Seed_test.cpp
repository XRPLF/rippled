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

#include <ripple/basics/random.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/rngfill.h>
#include <ripple/beast/xor_shift_engine.h>
#include <algorithm>


namespace ripple {

class Seed_test : public beast::unit_test::suite
{
    static
    bool equal(Seed const& lhs, Seed const& rhs)
    {
        return std::equal (
            lhs.data(), lhs.data() + lhs.size(),
            rhs.data(), rhs.data() + rhs.size());
    }

public:
    void testConstruction ()
    {
        testcase ("construction");

        {
            std::uint8_t src[16];

            for (std::uint8_t i = 0; i < 64; i++)
            {
                beast::rngfill (
                    src,
                    sizeof(src),
                    default_prng());
                Seed const seed ({ src, sizeof(src) });
                BEAST_EXPECT(memcmp (seed.data(), src, sizeof(src)) == 0);
            }
        }

        for (int i = 0; i < 64; i++)
        {
            uint128 src;
            beast::rngfill (
                src.data(),
                src.size(),
                default_prng());
            Seed const seed (src);
            BEAST_EXPECT(memcmp (seed.data(), src.data(), src.size()) == 0);
        }
    }

    std::string testPassphrase(std::string passphrase)
    {
        auto const seed1 = generateSeed (passphrase);
        auto const seed2 = parseBase58<Seed>(toBase58(seed1));

        BEAST_EXPECT(static_cast<bool>(seed2));
        BEAST_EXPECT(equal (seed1, *seed2));
        return toBase58(seed1);
    }

    void testPassphrase()
    {
        testcase ("generation from passphrase");
        BEAST_EXPECT(testPassphrase ("masterpassphrase") ==
            "snoPBrXtMeMyMHUVTgbuqAfg1SUTb");
        BEAST_EXPECT(testPassphrase ("Non-Random Passphrase") ==
            "snMKnVku798EnBwUfxeSD8953sLYA");
        BEAST_EXPECT(testPassphrase ("cookies excitement hand public") ==
            "sspUXGrmjQhq6mgc24jiRuevZiwKT");
    }

    void testBase58()
    {
        testcase ("base58 operations");

        // Success:
        BEAST_EXPECT(parseBase58<Seed>("snoPBrXtMeMyMHUVTgbuqAfg1SUTb"));
        BEAST_EXPECT(parseBase58<Seed>("snMKnVku798EnBwUfxeSD8953sLYA"));
        BEAST_EXPECT(parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwKT"));

        // Failure:
        BEAST_EXPECT(!parseBase58<Seed>(""));
        BEAST_EXPECT(!parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwK"));
        BEAST_EXPECT(!parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwKTT"));
        BEAST_EXPECT(!parseBase58<Seed>("sspOXGrmjQhq6mgc24jiRuevZiwKT"));
        BEAST_EXPECT(!parseBase58<Seed>("ssp/XGrmjQhq6mgc24jiRuevZiwKT"));
    }

    void testRandom()
    {
        testcase ("random generation");

        for (int i = 0; i < 32; i++)
        {
            auto const seed1 = randomSeed ();
            auto const seed2 = parseBase58<Seed>(toBase58(seed1));

            BEAST_EXPECT(static_cast<bool>(seed2));
            BEAST_EXPECT(equal (seed1, *seed2));
        }
    }

    void testKeypairGenerationAndSigning ()
    {
        std::string const message1 = "http://www.ripple.com";
        std::string const message2 = "https://www.ripple.com";

        {
            testcase ("Node keypair generation & signing (secp256k1)");

            auto const secretKey = generateSecretKey (
                KeyType::secp256k1, generateSeed ("masterpassphrase"));
            auto const publicKey = derivePublicKey (
                KeyType::secp256k1, secretKey);

            BEAST_EXPECT(toBase58(TokenType::NodePublic, publicKey) ==
                "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9");
            BEAST_EXPECT(toBase58(TokenType::NodePrivate, secretKey) ==
                "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe");
            BEAST_EXPECT(to_string(calcNodeID(publicKey)) ==
                "7E59C17D50F5959C7B158FEC95C8F815BF653DC8");

            auto sig = sign (publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify (publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify (publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey (
                    KeyType::secp256k1,
                    generateSecretKey (
                        KeyType::secp256k1,
                        generateSeed ("otherpassphrase")));

                BEAST_EXPECT(!verify (otherPublicKey, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify (publicKey, makeSlice(message1), sig));
            }
        }

        {
            testcase ("Node keypair generation & signing (ed25519)");

            auto const secretKey = generateSecretKey (
                KeyType::ed25519, generateSeed ("masterpassphrase"));
            auto const publicKey = derivePublicKey (
                KeyType::ed25519, secretKey);

            BEAST_EXPECT(toBase58(TokenType::NodePublic, publicKey) ==
                "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf");
            BEAST_EXPECT(toBase58(TokenType::NodePrivate, secretKey) ==
                "paKv46LztLqK3GaKz1rG2nQGN6M4JLyRtxFBYFTw4wAVHtGys36");
            BEAST_EXPECT(to_string(calcNodeID(publicKey)) ==
                "AA066C988C712815CC37AF71472B7CBBBD4E2A0A");

            auto sig = sign (publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify (publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify (publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey (
                    KeyType::ed25519,
                    generateSecretKey (
                        KeyType::ed25519,
                        generateSeed ("otherpassphrase")));

                BEAST_EXPECT(!verify (otherPublicKey, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify (publicKey, makeSlice(message1), sig));
            }
        }

        {
            testcase ("Account keypair generation & signing (secp256k1)");

            auto const [pk, sk] = generateKeyPair (
                KeyType::secp256k1,
                generateSeed ("masterpassphrase"));

            BEAST_EXPECT(toBase58(calcAccountID(pk)) ==
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
            BEAST_EXPECT(toBase58(TokenType::AccountPublic, pk) ==
                "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw");
            BEAST_EXPECT(toBase58(TokenType::AccountSecret, sk) ==
                "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh");

            auto sig = sign (pk, sk, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify (pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify (pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair = generateKeyPair (
                    KeyType::secp256k1,
                    generateSeed ("otherpassphrase"));

                BEAST_EXPECT(!verify (otherKeyPair.first, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify (pk, makeSlice(message1), sig));
            }
        }

        {
            testcase ("Account keypair generation & signing (ed25519)");

            auto const [pk, sk] = generateKeyPair (
                KeyType::ed25519,
                generateSeed ("masterpassphrase"));

            BEAST_EXPECT(to_string(calcAccountID(pk)) ==
                "rGWrZyQqhTp9Xu7G5Pkayo7bXjH4k4QYpf");
            BEAST_EXPECT(toBase58(TokenType::AccountPublic, pk) ==
                "aKGheSBjmCsKJVuLNKRAKpZXT6wpk2FCuEZAXJupXgdAxX5THCqR");
            BEAST_EXPECT(toBase58(TokenType::AccountSecret, sk) ==
                "pwDQjwEhbUBmPuEjFpEG75bFhv2obkCB7NxQsfFxM7xGHBMVPu9");

            auto sig = sign (pk, sk, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify (pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify (pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair = generateKeyPair (
                    KeyType::ed25519,
                    generateSeed ("otherpassphrase"));

                BEAST_EXPECT(!verify (otherKeyPair.first, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify (pk, makeSlice(message1), sig));
            }
        }
    }

    void testSeedParsing ()
    {
        testcase ("Parsing");

        // account IDs and node and account public and private
        // keys should not be parseable as seeds.

        auto const node1 = randomKeyPair(KeyType::secp256k1);

        BEAST_EXPECT(!parseGenericSeed (
            toBase58 (TokenType::NodePublic, node1.first)));
        BEAST_EXPECT(!parseGenericSeed (
            toBase58 (TokenType::NodePrivate, node1.second)));

        auto const node2 = randomKeyPair(KeyType::ed25519);

        BEAST_EXPECT(!parseGenericSeed (
            toBase58 (TokenType::NodePublic, node2.first)));
        BEAST_EXPECT(!parseGenericSeed (
            toBase58 (TokenType::NodePrivate, node2.second)));

        auto const account1 = generateKeyPair(
            KeyType::secp256k1, randomSeed ());

        BEAST_EXPECT(!parseGenericSeed (
            toBase58(calcAccountID(account1.first))));
        BEAST_EXPECT(!parseGenericSeed (
            toBase58(TokenType::AccountPublic, account1.first)));
        BEAST_EXPECT(!parseGenericSeed (
            toBase58(TokenType::AccountSecret, account1.second)));

        auto const account2 = generateKeyPair(
            KeyType::ed25519, randomSeed ());

        BEAST_EXPECT(!parseGenericSeed (
            toBase58(calcAccountID(account2.first))));
        BEAST_EXPECT(!parseGenericSeed (
            toBase58(TokenType::AccountPublic, account2.first)));
        BEAST_EXPECT(!parseGenericSeed (
            toBase58(TokenType::AccountSecret, account2.second)));
    }

    void run() override
    {
        testConstruction();
        testPassphrase();
        testBase58();
        testRandom();
        testKeypairGenerationAndSigning();
        testSeedParsing ();
    }
};

BEAST_DEFINE_TESTSUITE(Seed,protocol,ripple);

} // ripple
