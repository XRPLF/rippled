// Auto-generated unit tests for transaction TrustSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/TrustSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsTrustSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testTrustSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const limitAmountValue = canonical_AMOUNT();
    auto const qualityInValue = canonical_UINT32();
    auto const qualityOutValue = canonical_UINT32();

    TrustSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setLimitAmount(limitAmountValue);
    builder.setQualityIn(qualityInValue);
    builder.setQualityOut(qualityOutValue);

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
    // Verify optional fields
    {
        auto const& expected = limitAmountValue;
        auto const actualOpt = tx.getLimitAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLimitAmount should be present";
        expectEqualField(expected, *actualOpt, "sfLimitAmount");
        EXPECT_TRUE(tx.hasLimitAmount());
    }

    {
        auto const& expected = qualityInValue;
        auto const actualOpt = tx.getQualityIn();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfQualityIn should be present";
        expectEqualField(expected, *actualOpt, "sfQualityIn");
        EXPECT_TRUE(tx.hasQualityIn());
    }

    {
        auto const& expected = qualityOutValue;
        auto const actualOpt = tx.getQualityOut();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfQualityOut should be present";
        expectEqualField(expected, *actualOpt, "sfQualityOut");
        EXPECT_TRUE(tx.hasQualityOut());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsTrustSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testTrustSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const limitAmountValue = canonical_AMOUNT();
    auto const qualityInValue = canonical_UINT32();
    auto const qualityOutValue = canonical_UINT32();

    // Build an initial transaction
    TrustSetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setLimitAmount(limitAmountValue);
    initialBuilder.setQualityIn(qualityInValue);
    initialBuilder.setQualityOut(qualityOutValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    TrustSetBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    // Verify optional fields
    {
        auto const& expected = limitAmountValue;
        auto const actualOpt = rebuiltTx.getLimitAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLimitAmount should be present";
        expectEqualField(expected, *actualOpt, "sfLimitAmount");
    }

    {
        auto const& expected = qualityInValue;
        auto const actualOpt = rebuiltTx.getQualityIn();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfQualityIn should be present";
        expectEqualField(expected, *actualOpt, "sfQualityIn");
    }

    {
        auto const& expected = qualityOutValue;
        auto const actualOpt = rebuiltTx.getQualityOut();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfQualityOut should be present";
        expectEqualField(expected, *actualOpt, "sfQualityOut");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsTrustSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TrustSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsTrustSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TrustSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsTrustSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testTrustSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    TrustSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasLimitAmount());
    EXPECT_FALSE(tx.getLimitAmount().has_value());
    EXPECT_FALSE(tx.hasQualityIn());
    EXPECT_FALSE(tx.getQualityIn().has_value());
    EXPECT_FALSE(tx.hasQualityOut());
    EXPECT_FALSE(tx.getQualityOut().has_value());
}

}
