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

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <vector>

namespace ripple {

class PublicKey_test : public beast::unit_test::suite
{
public:
    using blob = std::vector<std::uint8_t>;

    template <class FwdIter, class Container>
    static void
    hex_to_binary(FwdIter first, FwdIter last, Container& out)
    {
        struct Table
        {
            int val[256];
            Table()
            {
                std::fill(val, val + 256, 0);
                for (int i = 0; i < 10; ++i)
                    val['0' + i] = i;
                for (int i = 0; i < 6; ++i)
                {
                    val['A' + i] = 10 + i;
                    val['a' + i] = 10 + i;
                }
            }
            int
            operator[](int i)
            {
                return val[i];
            }
        };

        static Table lut;
        out.reserve(std::distance(first, last) / 2);
        while (first != last)
        {
            auto const hi(lut[(*first++)]);
            auto const lo(lut[(*first++)]);
            out.push_back((hi * 16) + lo);
        }
    }

    blob
    sig(std::string const& hex)
    {
        blob b;
        hex_to_binary(hex.begin(), hex.end(), b);
        return b;
    }

    bool
    check(std::optional<ECDSACanonicality> answer, std::string const& s)
    {
        return ecdsaCanonicality(makeSlice(sig(s))) == answer;
    }

    void
    testCanonical()
    {
        testcase("Canonical");

        // Fully canonical
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3045"
            "022100FF478110D1D4294471EC76E0157540C2181F47DEBD25D7F9E7DDCCCD47EE"
            "E905"
            "0220078F07CDAE6C240855D084AD91D1479609533C147C93B0AEF19BC9724D003F"
            "28"));
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3045"
            "0221009218248292F1762D8A51BE80F8A7F2CD288D810CE781D5955700DA1684DF"
            "1D2D"
            "022041A1EE1746BFD72C9760CC93A7AAA8047D52C8833A03A20EAAE92EA19717B4"
            "54"));
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3044"
            "02206A9E43775F73B6D1EC420E4DDD222A80D4C6DF5D1BEECC431A91B63C928B75"
            "81"
            "022023E9CC2D61DDA6F73EAA6BCB12688BEB0F434769276B3127E4044ED895C9D9"
            "6B"));
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3044"
            "022056E720007221F3CD4EFBB6352741D8E5A0968D48D8D032C2FBC4F6304AD1D0"
            "4E"
            "02201F39EB392C20D7801C3E8D81D487E742FA84A1665E923225BD6323847C7187"
            "9F"));
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3045"
            "022100FDFD5AD05518CEA0017A2DCB5C4DF61E7C73B6D3A38E7AE93210A1564E8C"
            "2F12"
            "0220214FF061CCC123C81D0BB9D0EDEA04CD40D96BF1425D311DA62A7096BB18EA"
            "18"));

        // Canonical but not fully canonical
        BEAST_EXPECT(check(
            ECDSACanonicality::canonical,
            "3046"
            "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC"
            "0A1B"
            "022100928E6BCF1ED2684679730C5414AEC48FD62282B090041C41453C1D064AF5"
            "97A1"));
        BEAST_EXPECT(check(
            ECDSACanonicality::canonical,
            "3045"
            "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230"
            "B6"
            "0221008F2E8BB7D09521ABBC277717B14B93170AE6465C5A1B36561099319C4BEB"
            "254C"));
        BEAST_EXPECT(check(
            ECDSACanonicality::canonical,
            "3046"
            "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32"
            "625F"
            "022100897658A6B1F9EEE5D140D7A332DA0BD73BB98974EA53F6201B01C1B594F2"
            "86EA"));
        BEAST_EXPECT(check(
            ECDSACanonicality::canonical,
            "3045"
            "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AE"
            "C0"
            "022100DA4C6ADDEA14888858DE2AC5B91ED9050D6972BB388DEF582628CEE32869"
            "AE35"));

        // valid
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3006"
            "020101"
            "020102"));
        BEAST_EXPECT(check(
            ECDSACanonicality::fullyCanonical,
            "3044"
            "02203932c892e2e550f3af8ee4ce9c215a87f9bb831dcac87b2838e2c2eaa891df"
            "0c"
            "022030b61dd36543125d56b9f9f3a1f53189e5af33cdda8d77a5209aec03978fa0"
            "01"));
        BEAST_EXPECT(check(
            ECDSACanonicality::canonical,
            "3045"
            "0220076045be6f9eca28ff1ec606b833d0b87e70b2a630f5e3a496b110967a40f9"
            "0a"
            "0221008fffd599910eefe00bc803c688eca1d2ba7f6b180620eaa03488e6585db6"
            "ba01"));
        BEAST_EXPECT(check(
            ECDSACanonicality::canonical,
            "3046"
            "022100876045be6f9eca28ff1ec606b833d0b87e70b2a630f5e3a496b110967a40"
            "f90a"
            "0221008fffd599910eefe00bc803c688c2eca1d2ba7f6b180620eaa03488e6585d"
            "b6ba"));

        BEAST_EXPECT(check(
            std::nullopt,
            "3005"
            "0201FF"
            "0200"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020101"
            "020202"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020701"
            "020102"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020401"
            "020102"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020501"
            "020102"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020201"
            "020102"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020301"
            "020202"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3006"
            "020401"
            "020202"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3047"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba"
            "6105"
            "022200002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e56"
            "6695ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3144"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "301F"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed00"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3044"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3024"
            "0200"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3044"
            "02208990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3045"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba"
            "6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05012"
            "02d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695e"
            "d"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3024"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "0200"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "0220fd5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695"
            "ed"));
        BEAST_EXPECT(check(
            std::nullopt,
            "3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba61"
            "05"
            "0221002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e5666"
            "95ed"));
    }

    void
    testBase58(KeyType keyType)
    {
        // Try converting short, long and malformed data
        BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, ""));
        BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, " "));
        BEAST_EXPECT(
            !parseBase58<PublicKey>(TokenType::NodePublic, "!ty89234gh45"));

        auto const good = toBase58(
            TokenType::NodePublic, derivePublicKey(keyType, randomSecretKey()));

        // Short (non-empty) strings
        {
            auto s = good;

            // Remove all characters from the string in random order:
            std::hash<std::string> r;

            while (!s.empty())
            {
                s.erase(r(s) % s.size(), 1);
                BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, s));
            }
        }

        // Long strings
        for (std::size_t i = 1; i != 16; i++)
        {
            auto s = good;
            s.resize(s.size() + i, s[i % s.size()]);
            BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, s));
        }

        // Strings with invalid Base58 characters
        for (auto c : std::string("0IOl"))
        {
            for (std::size_t i = 0; i != good.size(); ++i)
            {
                auto s = good;
                s[i % s.size()] = c;
                BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, s));
            }
        }

        // Strings with incorrect prefix
        {
            auto s = good;

            for (auto c : std::string("apsrJqtv7"))
            {
                s[0] = c;
                BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, s));
            }
        }

        // Try some random secret keys
        std::vector<PublicKey> keys;
        keys.reserve(32);

        for (std::size_t i = 0; i != keys.capacity(); ++i)
            keys.emplace_back(derivePublicKey(keyType, randomSecretKey()));
        BEAST_EXPECT(keys.size() == 32);

        for (std::size_t i = 0; i != keys.size(); ++i)
        {
            auto const si = toBase58(TokenType::NodePublic, keys[i]);
            BEAST_EXPECT(!si.empty());

            auto const ski = parseBase58<PublicKey>(TokenType::NodePublic, si);
            BEAST_EXPECT(ski && (keys[i] == *ski));

            for (std::size_t j = i; j != keys.size(); ++j)
            {
                BEAST_EXPECT((keys[i] == keys[j]) == (i == j));

                auto const sj = toBase58(TokenType::NodePublic, keys[j]);

                BEAST_EXPECT((si == sj) == (i == j));

                auto const skj =
                    parseBase58<PublicKey>(TokenType::NodePublic, sj);
                BEAST_EXPECT(skj && (keys[j] == *skj));

                BEAST_EXPECT((*ski == *skj) == (i == j));
            }
        }
    }

    void
    testBase58()
    {
        testcase("Base58: secp256k1");

        {
            auto const pk1 = derivePublicKey(
                KeyType::secp256k1,
                generateSecretKey(
                    KeyType::secp256k1, generateSeed("masterpassphrase")));

            auto const pk2 = parseBase58<PublicKey>(
                TokenType::NodePublic,
                "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9");
            BEAST_EXPECT(pk2);

            BEAST_EXPECT(pk1 == *pk2);
        }

        testBase58(KeyType::secp256k1);

        testcase("Base58: ed25519");

        {
            auto const pk1 = derivePublicKey(
                KeyType::ed25519,
                generateSecretKey(
                    KeyType::ed25519, generateSeed("masterpassphrase")));

            auto const pk2 = parseBase58<PublicKey>(
                TokenType::NodePublic,
                "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf");
            BEAST_EXPECT(pk2);

            BEAST_EXPECT(pk1 == *pk2);
        }

        testBase58(KeyType::ed25519);
    }

    void
    testMiscOperations()
    {
        testcase("Miscellaneous operations");

        auto const pk1 = derivePublicKey(
            KeyType::secp256k1,
            generateSecretKey(
                KeyType::secp256k1, generateSeed("masterpassphrase")));

        PublicKey pk2(pk1);
        BEAST_EXPECT(pk1 == pk2);
        BEAST_EXPECT(pk2 == pk1);

        PublicKey pk3 = derivePublicKey(
            KeyType::secp256k1,
            generateSecretKey(
                KeyType::secp256k1, generateSeed("arbitraryPassPhrase")));
        // Testing the copy assignment operation of PublicKey class
        pk3 = pk2;
        BEAST_EXPECT(pk3 == pk2);
        BEAST_EXPECT(pk1 == pk3);
    }

    void
    run() override
    {
        testBase58();
        testCanonical();
        testMiscOperations();
    }
};

BEAST_DEFINE_TESTSUITE(PublicKey, protocol, ripple);

}  // namespace ripple
