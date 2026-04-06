// Auto-generated unit tests for transaction NFTokenCreateOffer


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/NFTokenCreateOffer.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsNFTokenCreateOfferTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenCreateOffer"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const nFTokenIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const ownerValue = canonical_ACCOUNT();
    auto const expirationValue = canonical_UINT32();

    NFTokenCreateOfferBuilder builder{
        accountValue,
        nFTokenIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestination(destinationValue);
    builder.setOwner(ownerValue);
    builder.setExpiration(expirationValue);

    auto tx = builder.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(tx.validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx.getSigningPubKey().empty());
    EXPECT_TRUE(tx.hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx.getAccount(), accountValue);
    EXPECT_EQ(tx.getSequence(), sequenceValue);
    EXPECT_EQ(tx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = nFTokenIDValue;
        auto const actual = tx.getNFTokenID();
        expectEqualField(expected, actual, "sfNFTokenID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx.hasDestination());
    }

    {
        auto const& expected = ownerValue;
        auto const actualOpt = tx.getOwner();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwner should be present";
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(tx.hasOwner());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx.hasExpiration());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsNFTokenCreateOfferTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenCreateOfferFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const nFTokenIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const ownerValue = canonical_ACCOUNT();
    auto const expirationValue = canonical_UINT32();

    // Build an initial transaction
    NFTokenCreateOfferBuilder initialBuilder{
        accountValue,
        nFTokenIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestination(destinationValue);
    initialBuilder.setOwner(ownerValue);
    initialBuilder.setExpiration(expirationValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    NFTokenCreateOfferBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = nFTokenIDValue;
        auto const actual = rebuiltTx.getNFTokenID();
        expectEqualField(expected, actual, "sfNFTokenID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = ownerValue;
        auto const actualOpt = rebuiltTx.getOwner();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwner should be present";
        expectEqualField(expected, *actualOpt, "sfOwner");
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsNFTokenCreateOfferTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(NFTokenCreateOffer{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsNFTokenCreateOfferTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(NFTokenCreateOfferBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsNFTokenCreateOfferTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenCreateOfferNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const nFTokenIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();

    NFTokenCreateOfferBuilder builder{
        accountValue,
        nFTokenIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDestination());
    EXPECT_FALSE(tx.getDestination().has_value());
    EXPECT_FALSE(tx.hasOwner());
    EXPECT_FALSE(tx.getOwner().has_value());
    EXPECT_FALSE(tx.hasExpiration());
    EXPECT_FALSE(tx.getExpiration().has_value());
}

}
