// Auto-generated unit tests for transaction AMMCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAMMCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const amountValue = canonical_AMOUNT();
    auto const amount2Value = canonical_AMOUNT();
    auto const tradingFeeValue = canonical_UINT16();
    auto const curveTypeValue = canonical_UINT8();
    auto const feeTierValue = canonical_UINT8();
    auto const amplificationValue = canonical_UINT32();
    auto const binStepValue = canonical_UINT16();

    AMMCreateBuilder builder{
        accountValue,
        amountValue,
        amount2Value,
        tradingFeeValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCurveType(curveTypeValue);
    builder.setFeeTier(feeTierValue);
    builder.setAmplification(amplificationValue);
    builder.setBinStep(binStepValue);

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
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = amount2Value;
        auto const actual = tx.getAmount2();
        expectEqualField(expected, actual, "sfAmount2");
    }

    {
        auto const& expected = tradingFeeValue;
        auto const actual = tx.getTradingFee();
        expectEqualField(expected, actual, "sfTradingFee");
    }

    // Verify optional fields
    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = tx.getCurveType();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCurveType should be present";
        expectEqualField(expected, *actualOpt, "sfCurveType");
        EXPECT_TRUE(tx.hasCurveType());
    }

    {
        auto const& expected = feeTierValue;
        auto const actualOpt = tx.getFeeTier();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFeeTier should be present";
        expectEqualField(expected, *actualOpt, "sfFeeTier");
        EXPECT_TRUE(tx.hasFeeTier());
    }

    {
        auto const& expected = amplificationValue;
        auto const actualOpt = tx.getAmplification();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmplification should be present";
        expectEqualField(expected, *actualOpt, "sfAmplification");
        EXPECT_TRUE(tx.hasAmplification());
    }

    {
        auto const& expected = binStepValue;
        auto const actualOpt = tx.getBinStep();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBinStep should be present";
        expectEqualField(expected, *actualOpt, "sfBinStep");
        EXPECT_TRUE(tx.hasBinStep());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAMMCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const amountValue = canonical_AMOUNT();
    auto const amount2Value = canonical_AMOUNT();
    auto const tradingFeeValue = canonical_UINT16();
    auto const curveTypeValue = canonical_UINT8();
    auto const feeTierValue = canonical_UINT8();
    auto const amplificationValue = canonical_UINT32();
    auto const binStepValue = canonical_UINT16();

    // Build an initial transaction
    AMMCreateBuilder initialBuilder{
        accountValue,
        amountValue,
        amount2Value,
        tradingFeeValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCurveType(curveTypeValue);
    initialBuilder.setFeeTier(feeTierValue);
    initialBuilder.setAmplification(amplificationValue);
    initialBuilder.setBinStep(binStepValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AMMCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = amount2Value;
        auto const actual = rebuiltTx.getAmount2();
        expectEqualField(expected, actual, "sfAmount2");
    }

    {
        auto const& expected = tradingFeeValue;
        auto const actual = rebuiltTx.getTradingFee();
        expectEqualField(expected, actual, "sfTradingFee");
    }

    // Verify optional fields
    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = rebuiltTx.getCurveType();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCurveType should be present";
        expectEqualField(expected, *actualOpt, "sfCurveType");
    }

    {
        auto const& expected = feeTierValue;
        auto const actualOpt = rebuiltTx.getFeeTier();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFeeTier should be present";
        expectEqualField(expected, *actualOpt, "sfFeeTier");
    }

    {
        auto const& expected = amplificationValue;
        auto const actualOpt = rebuiltTx.getAmplification();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmplification should be present";
        expectEqualField(expected, *actualOpt, "sfAmplification");
    }

    {
        auto const& expected = binStepValue;
        auto const actualOpt = rebuiltTx.getBinStep();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBinStep should be present";
        expectEqualField(expected, *actualOpt, "sfBinStep");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAMMCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAMMCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAMMCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const amountValue = canonical_AMOUNT();
    auto const amount2Value = canonical_AMOUNT();
    auto const tradingFeeValue = canonical_UINT16();

    AMMCreateBuilder builder{
        accountValue,
        amountValue,
        amount2Value,
        tradingFeeValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCurveType());
    EXPECT_FALSE(tx.getCurveType().has_value());
    EXPECT_FALSE(tx.hasFeeTier());
    EXPECT_FALSE(tx.getFeeTier().has_value());
    EXPECT_FALSE(tx.hasAmplification());
    EXPECT_FALSE(tx.getAmplification().has_value());
    EXPECT_FALSE(tx.hasBinStep());
    EXPECT_FALSE(tx.getBinStep().has_value());
}

}
