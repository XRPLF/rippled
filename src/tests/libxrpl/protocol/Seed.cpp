#include <xrpl/protocol/Seed.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/tokens.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

using namespace xrpl;

namespace {

// Seed has no operator==; compare bytes directly.
bool
equal(Seed const& lhs, Seed const& rhs)
{
    return std::equal(lhs.data(), lhs.data() + lhs.size(), rhs.data(), rhs.data() + rhs.size());
}

// Sign two messages, one a strict prefix of the other, with the given keypair
// and check that each signature verifies only against its own message and
// only under the signing key, and that a corrupted signature never verifies.
void
checkSignAndVerify(PublicKey const& pk, SecretKey const& sk, PublicKey const& otherPk)
{
    std::string_view msg = "b34d4fbb03b1e28f579701aeab43fead";

    Slice const m1{msg.data(), msg.size()};
    Slice const m2{msg.data(), msg.size() - 1};

    auto sig1 = sign(pk, sk, m1);
    auto sig2 = sign(pk, sk, m2);

    ASSERT_FALSE(sig2.empty());
    ASSERT_FALSE(sig1.empty());

    // Signing is deterministic, so equal signatures would mean that the
    // signer ignored the final byte.
    EXPECT_NE(sig2, sig1);

    // Correct public key: only the matching message verifies.
    EXPECT_TRUE(verify(pk, m2, sig2));
    EXPECT_FALSE(verify(pk, m2, sig1));
    EXPECT_FALSE(verify(pk, m1, sig2));
    EXPECT_TRUE(verify(pk, m1, sig1));

    // Wrong public key: nothing verifies.
    EXPECT_FALSE(verify(otherPk, m2, sig2));
    EXPECT_FALSE(verify(otherPk, m2, sig1));
    EXPECT_FALSE(verify(otherPk, m1, sig2));
    EXPECT_FALSE(verify(otherPk, m1, sig1));

    // Correct data & public key but corrupted signature.
    auto const expectAllCorruptionsFail = [&pk](Slice m, Buffer sig) {
        auto ptr = sig.data();

        for (std::size_t i = 0; i != sig.size(); ++i)
        {
            ptr[i] ^= 0x5A;
            EXPECT_FALSE(verify(pk, m, sig)) << "corrupted byte " << i << " of " << sig.size();
            ptr[i] ^= 0x5A;
        }
    };

    expectAllCorruptionsFail(m1, sig1);
    expectAllCorruptionsFail(m2, sig2);
}

struct MasterPassphraseCase
{
    KeyType type;
    std::string_view nodePublic;
    std::string_view nodePrivate;
    std::string_view nodeID;
    std::string_view accountID;
    std::string_view accountPublic;
    std::string_view accountSecret;
};

// Everything derived from the seed for "masterpassphrase".
constexpr auto kMasterPassphraseCases = std::to_array<MasterPassphraseCase>({
    {
        .type = KeyType::Secp256k1,
        .nodePublic = "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9",
        .nodePrivate = "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe",
        .nodeID = "7E59C17D50F5959C7B158FEC95C8F815BF653DC8",
        .accountID = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        .accountPublic = "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw",
        .accountSecret = "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh",
    },
    {
        .type = KeyType::Ed25519,
        .nodePublic = "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf",
        .nodePrivate = "paKv46LztLqK3GaKz1rG2nQGN6M4JLyRtxFBYFTw4wAVHtGys36",
        .nodeID = "AA066C988C712815CC37AF71472B7CBBBD4E2A0A",
        .accountID = "rGWrZyQqhTp9Xu7G5Pkayo7bXjH4k4QYpf",
        .accountPublic = "aKGheSBjmCsKJVuLNKRAKpZXT6wpk2FCuEZAXJupXgdAxX5THCqR",
        .accountSecret = "pwDQjwEhbUBmPuEjFpEG75bFhv2obkCB7NxQsfFxM7xGHBMVPu9",
    },
});

struct PassphraseCase
{
    std::string_view passphrase;
    std::string_view seed;
};

constexpr auto kPassphraseCases = std::to_array<PassphraseCase>({
    {.passphrase = "masterpassphrase", .seed = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb"},
    {.passphrase = "Non-Random Passphrase", .seed = "snMKnVku798EnBwUfxeSD8953sLYA"},
    {.passphrase = "cookies excitement hand public", .seed = "sspUXGrmjQhq6mgc24jiRuevZiwKT"},
});

}  // namespace

//------------------------------------------------------------------------------
// Construction

TEST(Seed, construction_from_a_slice_copies_the_bytes)
{
    for (int i = 0; i < 64; i++)
    {
        std::array<std::uint8_t, 16> src;
        beast::rngfill(src.data(), src.size(), defaultPrng());

        Seed const seed{makeSlice(src)};
        ASSERT_EQ(seed.size(), src.size());
        EXPECT_EQ(std::memcmp(seed.data(), src.data(), src.size()), 0);
    }
}

TEST(Seed, construction_from_a_uint128_copies_the_bytes)
{
    for (int i = 0; i < 64; i++)
    {
        UInt128 src;
        beast::rngfill(src.data(), src.size(), defaultPrng());

        Seed const seed{src};
        ASSERT_EQ(seed.size(), src.size());
        EXPECT_EQ(std::memcmp(seed.data(), src.data(), src.size()), 0);
    }
}

//------------------------------------------------------------------------------
// Passphrases

TEST(Seed, generation_from_passphrase_matches_the_test_vectors)
{
    for (auto const& test : kPassphraseCases)
    {
        SCOPED_TRACE(test.passphrase);

        auto const seed1 = generateSeed(std::string{test.passphrase});
        EXPECT_EQ(toBase58(seed1), test.seed);

        auto const seed2 = parseBase58<Seed>(toBase58(seed1));
        ASSERT_TRUE(seed2);
        EXPECT_TRUE(equal(seed1, *seed2));
    }
}

//------------------------------------------------------------------------------
// Base58

TEST(Seed, base58_parses_well_formed_seeds)
{
    for (auto const& test : kPassphraseCases)
        EXPECT_TRUE(parseBase58<Seed>(std::string{test.seed})) << test.seed;
}

TEST(Seed, base58_rejects_malformed_seeds)
{
    // Empty:
    EXPECT_FALSE(parseBase58<Seed>(""));

    // Too short:
    EXPECT_FALSE(parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwK"));

    // Too long:
    EXPECT_FALSE(parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwKTT"));

    // Invalid Base58 characters:
    EXPECT_FALSE(parseBase58<Seed>("sspOXGrmjQhq6mgc24jiRuevZiwKT"));
    EXPECT_FALSE(parseBase58<Seed>("ssp/XGrmjQhq6mgc24jiRuevZiwKT"));
}

TEST(Seed, base58_round_trips_random_seeds)
{
    for (int i = 0; i < 32; i++)
    {
        auto const seed1 = randomSeed();
        auto const seed2 = parseBase58<Seed>(toBase58(seed1));

        ASSERT_TRUE(seed2);
        EXPECT_TRUE(equal(seed1, *seed2));
    }
}

//------------------------------------------------------------------------------
// Key derivation from the master passphrase

class SeedMasterPassphraseTest : public ::testing::TestWithParam<MasterPassphraseCase>
{
};

TEST_P(SeedMasterPassphraseTest, node_keypair_generation_and_signing)
{
    auto const& test = GetParam();

    auto const secretKey = generateSecretKey(test.type, generateSeed("masterpassphrase"));
    auto const publicKey = derivePublicKey(test.type, secretKey);

    EXPECT_EQ(toBase58(TokenType::NodePublic, publicKey), test.nodePublic);
    EXPECT_EQ(toBase58(TokenType::NodePrivate, secretKey), test.nodePrivate);
    EXPECT_EQ(to_string(calcNodeID(publicKey)), test.nodeID);

    auto const otherPublicKey =
        derivePublicKey(test.type, generateSecretKey(test.type, generateSeed("otherpassphrase")));

    checkSignAndVerify(publicKey, secretKey, otherPublicKey);
}

TEST_P(SeedMasterPassphraseTest, account_keypair_generation_and_signing)
{
    auto const& test = GetParam();

    auto const [pk, sk] = generateKeyPair(test.type, generateSeed("masterpassphrase"));

    EXPECT_EQ(toBase58(calcAccountID(pk)), test.accountID);
    EXPECT_EQ(toBase58(TokenType::AccountPublic, pk), test.accountPublic);
    EXPECT_EQ(toBase58(TokenType::AccountSecret, sk), test.accountSecret);

    auto const otherKeyPair = generateKeyPair(test.type, generateSeed("otherpassphrase"));

    checkSignAndVerify(pk, sk, otherKeyPair.first);
}

INSTANTIATE_TEST_SUITE_P(
    Seed,
    SeedMasterPassphraseTest,
    ::testing::ValuesIn(kMasterPassphraseCases),
    [](::testing::TestParamInfo<MasterPassphraseCase> const& info) {
        return std::string{to_string(info.param.type)};
    });

//------------------------------------------------------------------------------
// Parsing

class SeedParsingTest : public ::testing::TestWithParam<KeyType>
{
};

TEST_P(SeedParsingTest, node_keys_are_not_parsable_as_seeds)
{
    auto const [pk, sk] = randomKeyPair(GetParam());

    EXPECT_FALSE(parseGenericSeed(toBase58(TokenType::NodePublic, pk)));
    EXPECT_FALSE(parseGenericSeed(toBase58(TokenType::NodePrivate, sk)));
}

TEST_P(SeedParsingTest, account_keys_and_ids_are_not_parsable_as_seeds)
{
    auto const [pk, sk] = generateKeyPair(GetParam(), randomSeed());

    EXPECT_FALSE(parseGenericSeed(toBase58(calcAccountID(pk))));
    EXPECT_FALSE(parseGenericSeed(toBase58(TokenType::AccountPublic, pk)));
    EXPECT_FALSE(parseGenericSeed(toBase58(TokenType::AccountSecret, sk)));
}

INSTANTIATE_TEST_SUITE_P(
    Seed,
    SeedParsingTest,
    ::testing::Values(KeyType::Secp256k1, KeyType::Ed25519),
    [](::testing::TestParamInfo<KeyType> const& info) {
        return std::string{to_string(info.param)};
    });
