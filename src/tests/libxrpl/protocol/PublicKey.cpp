#include <xrpl/protocol/PublicKey.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace xrpl;

namespace {

Blob
unhex(std::string_view hex)
{
    auto blob = strUnHex(hex);

    if (!blob)
        throw std::invalid_argument("bad hex in test vector");

    return std::move(*blob);
}

// Node public keys derived from "masterpassphrase".
constexpr std::string_view kMasterNodePublicSecp256k1 =
    "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9";
constexpr std::string_view kMasterNodePublicEd25519 =
    "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf";

std::string_view
masterNodePublic(KeyType type)
{
    return type == KeyType::Secp256k1 ? kMasterNodePublicSecp256k1 : kMasterNodePublicEd25519;
}

PublicKey
masterPublicKey(KeyType type)
{
    return derivePublicKey(type, generateSecretKey(type, generateSeed("masterpassphrase")));
}

// Adds `addend` to the little-endian integer in `value`, in place, discarding
// any final carry.
void
addLittleEndian(std::array<std::uint8_t, 32>& value, std::array<std::uint8_t, 32> const& addend)
{
    unsigned carry = 0;

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        unsigned const sum = value[i] + addend[i] + carry;
        value[i] = static_cast<std::uint8_t>(sum & 0xFF);
        carry = sum >> 8;
    }
}

}  // namespace

//------------------------------------------------------------------------------
// publicKeyType

TEST(PublicKey, public_key_type_recognizes_both_compressed_secp256k1_prefixes)
{
    std::array<std::uint8_t, 33> key{};

    key[0] = 0x02;
    EXPECT_EQ(publicKeyType(makeSlice(key)), KeyType::Secp256k1);

    key[0] = 0x03;
    EXPECT_EQ(publicKeyType(makeSlice(key)), KeyType::Secp256k1);
}

TEST(PublicKey, public_key_type_recognizes_the_ed25519_prefix)
{
    std::array<std::uint8_t, 33> key{};
    key[0] = 0xED;

    EXPECT_EQ(publicKeyType(makeSlice(key)), KeyType::Ed25519);
}

TEST(PublicKey, public_key_type_rejects_unknown_prefixes)
{
    std::array<std::uint8_t, 33> key{};

    for (unsigned prefix = 0; prefix <= 0xFF; ++prefix)
    {
        if (prefix == 0x02 || prefix == 0x03 || prefix == 0xED)
            continue;

        key[0] = static_cast<std::uint8_t>(prefix);
        EXPECT_FALSE(publicKeyType(makeSlice(key))) << "prefix 0x" << std::hex << prefix;
    }
}

TEST(PublicKey, public_key_type_rejects_wrong_lengths)
{
    EXPECT_FALSE(publicKeyType(Slice{}));

    std::array<std::uint8_t, 65> const key{0x02};

    for (std::size_t len = 0; len <= key.size(); ++len)
    {
        if (len == 33)
            continue;

        EXPECT_FALSE(publicKeyType(Slice{key.data(), len})) << "length " << len;
    }
}

//------------------------------------------------------------------------------
// Copy semantics

TEST(PublicKey, copy_construction_and_assignment)
{
    auto const pk1 = masterPublicKey(KeyType::Secp256k1);

    PublicKey const pk2(pk1);
    EXPECT_EQ(pk1, pk2);
    EXPECT_EQ(pk2, pk1);

    PublicKey pk3 = derivePublicKey(
        KeyType::Secp256k1,
        generateSecretKey(KeyType::Secp256k1, generateSeed("arbitraryPassPhrase")));
    EXPECT_NE(pk3, pk1);

    pk3 = pk2;
    EXPECT_EQ(pk3, pk2);
    EXPECT_EQ(pk3, pk1);
}

//------------------------------------------------------------------------------
// Base58

class PublicKeyBase58Test : public ::testing::TestWithParam<KeyType>
{
};

TEST_P(PublicKeyBase58Test, parses_the_well_known_master_key)
{
    auto const pk =
        parseBase58<PublicKey>(TokenType::NodePublic, std::string{masterNodePublic(GetParam())});

    ASSERT_TRUE(pk);
    EXPECT_EQ(*pk, masterPublicKey(GetParam()));
    EXPECT_EQ(toBase58(TokenType::NodePublic, *pk), masterNodePublic(GetParam()));
}

TEST_P(PublicKeyBase58Test, rejects_garbage)
{
    EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, ""));
    EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, " "));
    EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, "!ty89234gh45"));
}

TEST_P(PublicKeyBase58Test, rejects_truncated_strings)
{
    auto s = toBase58(TokenType::NodePublic, derivePublicKey(GetParam(), randomSecretKey()));

    // Remove all characters from the string in random order:
    std::hash<std::string> const r;

    while (!s.empty())
    {
        s.erase(r(s) % s.size(), 1);
        EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, s)) << s;
    }
}

TEST_P(PublicKeyBase58Test, rejects_lengthened_strings)
{
    auto const good =
        toBase58(TokenType::NodePublic, derivePublicKey(GetParam(), randomSecretKey()));

    for (std::size_t i = 1; i != 16; i++)
    {
        auto s = good;
        s.resize(s.size() + i, s[i % s.size()]);
        EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, s)) << s;
    }
}

TEST_P(PublicKeyBase58Test, rejects_invalid_base58_characters)
{
    auto const good =
        toBase58(TokenType::NodePublic, derivePublicKey(GetParam(), randomSecretKey()));

    for (auto const c : std::string_view{"0IOl"})
    {
        for (std::size_t i = 0; i != good.size(); ++i)
        {
            auto s = good;
            s[i] = c;
            EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, s)) << s;
        }
    }
}

TEST_P(PublicKeyBase58Test, rejects_incorrect_prefix)
{
    auto s = toBase58(TokenType::NodePublic, derivePublicKey(GetParam(), randomSecretKey()));

    for (auto const c : std::string_view{"apsrJqtv7"})
    {
        s[0] = c;
        EXPECT_FALSE(parseBase58<PublicKey>(TokenType::NodePublic, s)) << s;
    }
}

TEST_P(PublicKeyBase58Test, round_trips_random_keys)
{
    std::vector<PublicKey> keys;
    keys.reserve(32);

    for (std::size_t i = 0; i != keys.capacity(); ++i)
        keys.emplace_back(derivePublicKey(GetParam(), randomSecretKey()));

    for (std::size_t i = 0; i != keys.size(); ++i)
    {
        auto const si = toBase58(TokenType::NodePublic, keys[i]);
        EXPECT_FALSE(si.empty());

        auto const ski = parseBase58<PublicKey>(TokenType::NodePublic, si);
        ASSERT_TRUE(ski);
        EXPECT_EQ(keys[i], *ski);

        for (std::size_t j = i; j != keys.size(); ++j)
        {
            EXPECT_EQ(keys[i] == keys[j], i == j);

            auto const sj = toBase58(TokenType::NodePublic, keys[j]);
            EXPECT_EQ(si == sj, i == j);

            auto const skj = parseBase58<PublicKey>(TokenType::NodePublic, sj);
            ASSERT_TRUE(skj);
            EXPECT_EQ(keys[j], *skj);
            EXPECT_EQ(*ski == *skj, i == j);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    PublicKey,
    PublicKeyBase58Test,
    ::testing::Values(KeyType::Secp256k1, KeyType::Ed25519),
    [](::testing::TestParamInfo<KeyType> const& info) {
        return std::string{to_string(info.param)};
    });

//------------------------------------------------------------------------------
// verify: cross-type and malformed inputs

TEST(PublicKey, verify_rejects_a_signature_made_with_the_other_key_type)
{
    auto const [secpPk, secpSk] = randomKeyPair(KeyType::Secp256k1);
    auto const [edPk, edSk] = randomKeyPair(KeyType::Ed25519);

    std::string const message = "https://xrpl.org";

    auto const secpSig = sign(secpPk, secpSk, makeSlice(message));
    auto const edSig = sign(edPk, edSk, makeSlice(message));

    ASSERT_TRUE(verify(secpPk, makeSlice(message), secpSig));
    ASSERT_TRUE(verify(edPk, makeSlice(message), edSig));

    EXPECT_FALSE(verify(secpPk, makeSlice(message), edSig));
    EXPECT_FALSE(verify(edPk, makeSlice(message), secpSig));
}

TEST(PublicKey, verify_rejects_an_empty_signature)
{
    std::string const message = "https://xrpl.org";

    for (auto const type : {KeyType::Secp256k1, KeyType::Ed25519})
    {
        auto const [pk, sk] = randomKeyPair(type);
        EXPECT_FALSE(verify(pk, makeSlice(message), Slice{})) << to_string(type);
    }
}

TEST(PublicKey, verify_rejects_an_ed25519_signature_of_the_wrong_length)
{
    auto const [pk, sk] = randomKeyPair(KeyType::Ed25519);

    std::string const message = "https://xrpl.org";
    auto const sig = sign(pk, sk, makeSlice(message));
    ASSERT_EQ(sig.size(), 64);
    ASSERT_TRUE(verify(pk, makeSlice(message), sig));

    // One byte short:
    EXPECT_FALSE(verify(pk, makeSlice(message), Slice{sig.data(), sig.size() - 1}));

    // One byte long:
    std::vector<std::uint8_t> longer(sig.data(), sig.data() + sig.size());
    longer.push_back(0);
    EXPECT_FALSE(verify(pk, makeSlice(message), makeSlice(longer)));
}

TEST(PublicKey, verify_rejects_an_ed25519_signature_with_non_canonical_s)
{
    // L, the order of the prime-order subgroup, in little-endian byte order
    // to match the encoding of S in the signature.
    std::array<std::uint8_t, 32> order;
    {
        auto const be = unhex("1000000000000000000000000000000014DEF9DEA2F79CD65812631A5CF5D3ED");
        ASSERT_EQ(be.size(), order.size());
        std::ranges::reverse_copy(be, order.begin());
    }

    std::string const message = "https://xrpl.org";

    for (int i = 0; i < 16; ++i)
    {
        auto const [pk, sk] = randomKeyPair(KeyType::Ed25519);

        auto const sig = sign(pk, sk, makeSlice(message));
        ASSERT_EQ(sig.size(), 64);
        ASSERT_TRUE(verify(pk, makeSlice(message), sig));

        // A conforming signer produces S < L. Adding L gives S' = S + L,
        // which is congruent to S and would be accepted by a verifier that
        // reduces S modulo L (as ed25519-donna does). RFC 8032 requires it be
        // rejected.
        std::array<std::uint8_t, 64> mutated;
        std::ranges::copy(sig.data(), sig.data() + sig.size(), mutated.begin());

        std::array<std::uint8_t, 32> s;
        std::ranges::copy(mutated.begin() + 32, mutated.end(), s.begin());
        addLittleEndian(s, order);
        std::ranges::copy(s, mutated.begin() + 32);

        EXPECT_FALSE(verify(pk, makeSlice(message), makeSlice(mutated)));
    }
}

TEST(PublicKey, verify_rejects_an_ed25519_s_equal_to_the_group_order)
{
    auto const [pk, sk] = randomKeyPair(KeyType::Ed25519);

    std::string const message = "https://xrpl.org";
    auto const sig = sign(pk, sk, makeSlice(message));
    ASSERT_EQ(sig.size(), 64);

    // S == L is not strictly less than L and must be rejected regardless of
    // whether the rest of the signature is meaningful.
    std::array<std::uint8_t, 64> mutated;
    std::ranges::copy(sig.data(), sig.data() + sig.size(), mutated.begin());

    auto const be = unhex("1000000000000000000000000000000014DEF9DEA2F79CD65812631A5CF5D3ED");
    ASSERT_EQ(be.size(), 32);
    std::ranges::reverse_copy(be, mutated.begin() + 32);

    EXPECT_FALSE(verify(pk, makeSlice(message), makeSlice(mutated)));
}

//------------------------------------------------------------------------------
// verifyDigest: malformed DER

struct MalformedDerCase
{
    std::string_view name;
    std::string_view hex;
};

// Every one of these must fail verification against any key and any digest,
// either because strict DER parsing rejects the encoding or because R or S is
// outside [1, n-1] and libsecp256k1 guarantees such signatures never verify.
//
// These vectors are unrelated to the key and digest they are checked against,
// so they would fail to verify even if they were well-formed. They only show
// that malformed input is handled and never accepted; the test that follows
// them is the one that detects a parser which is not strict.
constexpr auto kMalformedDerCases = std::to_array<MalformedDerCase>({
    {.name = "empty", .hex = ""},
    {.name = "lone_sequence_tag", .hex = "30"},
    {.name = "sequence_with_zero_length", .hex = "3000"},
    {.name = "negative_r_and_empty_s",
     .hex = "3005"
            "0201FF"
            "0200"},
    {.name = "s_length_overruns",
     .hex = "3006"
            "020101"
            "020202"},
    {.name = "r_length_overruns_by_a_lot",
     .hex = "3006"
            "020701"
            "020102"},
    {.name = "r_length_overruns_by_four",
     .hex = "3006"
            "020401"
            "020102"},
    {.name = "r_length_overruns_by_five",
     .hex = "3006"
            "020501"
            "020102"},
    {.name = "r_length_overruns_by_two",
     .hex = "3006"
            "020201"
            "020102"},
    {.name = "r_length_three_s_length_overruns",
     .hex = "3006"
            "020301"
            "020202"},
    {.name = "r_length_four_s_length_overruns",
     .hex = "3006"
            "020401"
            "020202"},
    {.name = "zero_r",
     .hex = "3006"
            "020100"
            "020102"},
    {.name = "zero_s",
     .hex = "3006"
            "020101"
            "020100"},
    {.name = "empty_r",
     .hex = "3024"
            "0200"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "empty_s",
     .hex = "3024"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0200"},
    {.name = "wrong_sequence_tag",
     .hex = "3144"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "sequence_length_too_long",
     .hex = "3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "sequence_length_too_short",
     .hex = "3043"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "trailing_byte_inside_sequence",
     .hex = "3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"
            "00"},
    {.name = "trailing_byte_after_sequence",
     .hex = "3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"
            "00"},
    {.name = "wrong_tag_for_r",
     .hex = "3044"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "wrong_tag_for_s",
     .hex = "3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "01202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "wrong_tag_for_r_and_truncated",
     .hex = "301F"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1"},
    {.name = "negative_r",
     .hex = "3044"
            "02208990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "negative_s",
     .hex = "3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0220fd5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "excess_zero_padding_on_r",
     .hex = "3045"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "excess_zero_padding_on_s",
     .hex = "3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0221002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "two_bytes_of_zero_padding_on_s",
     .hex = "3047"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "022200002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
    {.name = "r_equal_to_the_curve_order",
     .hex = "3026"
            "022100FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141"
            "020101"},
    {.name = "s_equal_to_the_curve_order",
     .hex = "3026"
            "020101"
            "022100FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141"},
    {.name = "r_longer_than_33_bytes",
     .hex = "3047"
            "02230100000000000000000000000000000000000000000000000000000000000000000000"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"},
});

TEST(PublicKey, verify_digest_rejects_malformed_der)
{
    // The key and digest are arbitrary: a malformed signature must never
    // verify against anything.
    auto const [pk, sk] = randomKeyPair(KeyType::Secp256k1);

    UInt256 digest;
    beast::rngfill(digest.data(), digest.size(), cryptoPrng());

    for (auto const& c : kMalformedDerCases)
    {
        auto const sig = unhex(c.hex);

        EXPECT_FALSE(verifyDigest(pk, digest, makeSlice(sig), false)) << c.name;
        EXPECT_FALSE(verifyDigest(pk, digest, makeSlice(sig), true)) << c.name;
    }
}

//------------------------------------------------------------------------------
// verifyDigest: strict DER

TEST(PublicKey, verify_digest_rejects_non_strict_der_of_a_valid_signature)
{
    auto const [pk, sk] = randomKeyPair(KeyType::Secp256k1);

    UInt256 digest;
    beast::rngfill(digest.data(), digest.size(), cryptoPrng());

    auto const sig = signDigest(pk, sk, digest);
    Blob const good(sig.data(), sig.data() + sig.size());

    // The baseline must verify, or the rejections below prove nothing.
    ASSERT_TRUE(verifyDigest(pk, digest, makeSlice(good), false));
    ASSERT_TRUE(verifyDigest(pk, digest, makeSlice(good), true));

    // The layout of a DER signature is fixed, and only the length of R moves
    // what follows it: 30 <len> 02 <lenR> <R> 02 <lenS> <S>
    constexpr std::size_t kSeqLen = 1;
    constexpr std::size_t kRLen = 3;
    constexpr std::size_t kROff = 4;
    std::size_t const sLen = kROff + good[kRLen] + 1;
    std::size_t const sOff = sLen + 1;

    auto const insert = [](Blob& b, std::size_t at, std::uint8_t value) {
        b.insert(b.begin() + static_cast<std::ptrdiff_t>(at), value);
    };

    struct Case
    {
        std::string_view name;
        std::function<void(Blob&)> mutate;
    };

    // Every case still encodes the R and S of the valid signature, so a parser
    // that tolerated the encoding would recover them and the signature would
    // verify.
    Case const cases[] = {
        {.name = "R has an extra leading zero",
         .mutate =
             [&](Blob& b) {
                 insert(b, kROff, 0x00);
                 ++b[kRLen];
                 ++b[kSeqLen];
             }},
        {.name = "S has an extra leading zero",
         .mutate =
             [&](Blob& b) {
                 insert(b, sOff, 0x00);
                 ++b[sLen];
                 ++b[kSeqLen];
             }},
        {.name = "trailing byte after the sequence", .mutate = [](Blob& b) { b.push_back(0x00); }},
        {.name = "trailing byte inside the sequence",
         .mutate =
             [&](Blob& b) {
                 b.push_back(0x00);
                 ++b[kSeqLen];
             }},
        {.name = "long-form sequence length", .mutate = [&](Blob& b) { insert(b, kSeqLen, 0x81); }},
        {.name = "long-form R length",
         .mutate =
             [&](Blob& b) {
                 insert(b, kRLen, 0x81);
                 ++b[kSeqLen];
             }},
        {.name = "long-form S length",
         .mutate =
             [&](Blob& b) {
                 insert(b, sLen, 0x81);
                 ++b[kSeqLen];
             }},
        {.name = "indefinite sequence length",
         .mutate =
             [&](Blob& b) {
                 b[kSeqLen] = 0x80;
                 b.push_back(0x00);
                 b.push_back(0x00);
             }},
        {.name = "sequence length one too small", .mutate = [&](Blob& b) { --b[kSeqLen]; }},
        {.name = "sequence length one too large", .mutate = [&](Blob& b) { ++b[kSeqLen]; }},
    };

    for (auto const& c : cases)
    {
        Blob bad = good;
        c.mutate(bad);

        EXPECT_FALSE(verifyDigest(pk, digest, makeSlice(bad), false)) << c.name;
        EXPECT_FALSE(verifyDigest(pk, digest, makeSlice(bad), true)) << c.name;
    }
}
