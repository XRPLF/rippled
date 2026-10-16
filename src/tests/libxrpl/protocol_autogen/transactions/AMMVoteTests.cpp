// Auto-generated unit tests for transaction AMMVote


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMVote.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAMMVoteTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMVote"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const tradingFeeValue = canonical_UINT16();
    auto const curveTypeValue = canonical_UINT8();
    auto const amplificationValue = canonical_UINT32();

    AMMVoteBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setTradingFee(tradingFeeValue);
    builder.setCurveType(curveTypeValue);
    builder.setAmplification(amplificationValue);

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
        auto const& expected = assetValue;
        auto const actual = tx.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = tx.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = tradingFeeValue;
        auto const actualOpt = tx.getTradingFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTradingFee should be present";
        expectEqualField(expected, *actualOpt, "sfTradingFee");
        EXPECT_TRUE(tx.hasTradingFee());
    }

    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = tx.getCurveType();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCurveType should be present";
        expectEqualField(expected, *actualOpt, "sfCurveType");
        EXPECT_TRUE(tx.hasCurveType());
    }

    {
        auto const& expected = amplificationValue;
        auto const actualOpt = tx.getAmplification();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmplification should be present";
        expectEqualField(expected, *actualOpt, "sfAmplification");
        EXPECT_TRUE(tx.hasAmplification());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAMMVoteTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMVoteFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const tradingFeeValue = canonical_UINT16();
    auto const curveTypeValue = canonical_UINT8();
    auto const amplificationValue = canonical_UINT32();

    // Build an initial transaction
    AMMVoteBuilder initialBuilder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    initialBuilder.setTradingFee(tradingFeeValue);
    initialBuilder.setCurveType(curveTypeValue);
    initialBuilder.setAmplification(amplificationValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AMMVoteBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = assetValue;
        auto const actual = rebuiltTx.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = rebuiltTx.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = tradingFeeValue;
        auto const actualOpt = rebuiltTx.getTradingFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTradingFee should be present";
        expectEqualField(expected, *actualOpt, "sfTradingFee");
    }

    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = rebuiltTx.getCurveType();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCurveType should be present";
        expectEqualField(expected, *actualOpt, "sfCurveType");
    }

    {
        auto const& expected = amplificationValue;
        auto const actualOpt = rebuiltTx.getAmplification();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmplification should be present";
        expectEqualField(expected, *actualOpt, "sfAmplification");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAMMVoteTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMVote{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAMMVoteTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMVoteBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAMMVoteTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMVoteNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();

    AMMVoteBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasTradingFee());
    EXPECT_FALSE(tx.getTradingFee().has_value());
    EXPECT_FALSE(tx.hasCurveType());
    EXPECT_FALSE(tx.getCurveType().has_value());
    EXPECT_FALSE(tx.hasAmplification());
    EXPECT_FALSE(tx.getAmplification().has_value());
}

}
