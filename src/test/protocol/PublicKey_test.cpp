#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

class PublicKey_test : public beast::unit_test::Suite
{
public:
    void
    testBase58(KeyType keyType)
    {
        // Try converting short, long and malformed data
        BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, ""));
        BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, " "));
        BEAST_EXPECT(!parseBase58<PublicKey>(TokenType::NodePublic, "!ty89234gh45"));

        auto const good =
            toBase58(TokenType::NodePublic, derivePublicKey(keyType, randomSecretKey()));

        // Short (non-empty) strings
        {
            auto s = good;

            // Remove all characters from the string in random order:
            std::hash<std::string> const r;

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
        for (auto const c : std::string("0IOl"))
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

            for (auto const c : std::string("apsrJqtv7"))
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

                auto const skj = parseBase58<PublicKey>(TokenType::NodePublic, sj);
                BEAST_EXPECT(skj && (keys[j] == *skj));

                BEAST_EXPECT(
                    (*ski == *skj) == (i == j));  // NOLINT(bugprone-unchecked-optional-access)
            }
        }
    }

    void
    testBase58()
    {
        testcase("Base58: secp256k1");

        {
            auto const pk1 = derivePublicKey(
                KeyType::Secp256k1,
                generateSecretKey(KeyType::Secp256k1, generateSeed("masterpassphrase")));

            auto const pk2 = parseBase58<PublicKey>(
                TokenType::NodePublic, "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9");
            BEAST_EXPECT(pk2);

            BEAST_EXPECT(pk1 == *pk2);  // NOLINT(bugprone-unchecked-optional-access)
        }

        testBase58(KeyType::Secp256k1);

        testcase("Base58: ed25519");

        {
            auto const pk1 = derivePublicKey(
                KeyType::Ed25519,
                generateSecretKey(KeyType::Ed25519, generateSeed("masterpassphrase")));

            auto const pk2 = parseBase58<PublicKey>(
                TokenType::NodePublic, "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf");
            BEAST_EXPECT(pk2);

            BEAST_EXPECT(pk1 == *pk2);  // NOLINT(bugprone-unchecked-optional-access)
        }

        testBase58(KeyType::Ed25519);
    }

    void
    testMiscOperations()
    {
        testcase("Miscellaneous operations");

        auto const pk1 = derivePublicKey(
            KeyType::Secp256k1,
            generateSecretKey(KeyType::Secp256k1, generateSeed("masterpassphrase")));

        PublicKey const pk2(pk1);
        BEAST_EXPECT(pk1 == pk2);
        BEAST_EXPECT(pk2 == pk1);

        PublicKey pk3 = derivePublicKey(
            KeyType::Secp256k1,
            generateSecretKey(KeyType::Secp256k1, generateSeed("arbitraryPassPhrase")));
        // Testing the copy assignment operation of PublicKey class
        pk3 = pk2;
        BEAST_EXPECT(pk3 == pk2);
        BEAST_EXPECT(pk1 == pk3);
    }

    void
    run() override
    {
        testBase58();
        testMiscOperations();
    }
};

BEAST_DEFINE_TESTSUITE(PublicKey, protocol, xrpl);

}  // namespace xrpl
