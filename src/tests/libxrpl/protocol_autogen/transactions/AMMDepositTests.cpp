// Auto-generated unit tests for transaction AMMDeposit


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMDeposit.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAMMDepositTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMDeposit"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const amountValue = canonical_AMOUNT();
    auto const amount2Value = canonical_AMOUNT();
    auto const ePriceValue = canonical_AMOUNT();
    auto const lPTokenOutValue = canonical_AMOUNT();
    auto const tradingFeeValue = canonical_UINT16();

    AMMDepositBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAmount(amountValue);
    builder.setAmount2(amount2Value);
    builder.setEPrice(ePriceValue);
    builder.setLPTokenOut(lPTokenOutValue);
    builder.setTradingFee(tradingFeeValue);

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
        auto const& expected = amountValue;
        auto const actualOpt = tx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(tx.hasAmount());
    }

    {
        auto const& expected = amount2Value;
        auto const actualOpt = tx.getAmount2();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount2 should be present";
        expectEqualField(expected, *actualOpt, "sfAmount2");
        EXPECT_TRUE(tx.hasAmount2());
    }

    {
        auto const& expected = ePriceValue;
        auto const actualOpt = tx.getEPrice();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfEPrice should be present";
        expectEqualField(expected, *actualOpt, "sfEPrice");
        EXPECT_TRUE(tx.hasEPrice());
    }

    {
        auto const& expected = lPTokenOutValue;
        auto const actualOpt = tx.getLPTokenOut();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLPTokenOut should be present";
        expectEqualField(expected, *actualOpt, "sfLPTokenOut");
        EXPECT_TRUE(tx.hasLPTokenOut());
    }

    {
        auto const& expected = tradingFeeValue;
        auto const actualOpt = tx.getTradingFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTradingFee should be present";
        expectEqualField(expected, *actualOpt, "sfTradingFee");
        EXPECT_TRUE(tx.hasTradingFee());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAMMDepositTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMDepositFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const amountValue = canonical_AMOUNT();
    auto const amount2Value = canonical_AMOUNT();
    auto const ePriceValue = canonical_AMOUNT();
    auto const lPTokenOutValue = canonical_AMOUNT();
    auto const tradingFeeValue = canonical_UINT16();

    // Build an initial transaction
    AMMDepositBuilder initialBuilder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAmount(amountValue);
    initialBuilder.setAmount2(amount2Value);
    initialBuilder.setEPrice(ePriceValue);
    initialBuilder.setLPTokenOut(lPTokenOutValue);
    initialBuilder.setTradingFee(tradingFeeValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AMMDepositBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

    {
        auto const& expected = amount2Value;
        auto const actualOpt = rebuiltTx.getAmount2();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount2 should be present";
        expectEqualField(expected, *actualOpt, "sfAmount2");
    }

    {
        auto const& expected = ePriceValue;
        auto const actualOpt = rebuiltTx.getEPrice();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfEPrice should be present";
        expectEqualField(expected, *actualOpt, "sfEPrice");
    }

    {
        auto const& expected = lPTokenOutValue;
        auto const actualOpt = rebuiltTx.getLPTokenOut();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLPTokenOut should be present";
        expectEqualField(expected, *actualOpt, "sfLPTokenOut");
    }

    {
        auto const& expected = tradingFeeValue;
        auto const actualOpt = rebuiltTx.getTradingFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTradingFee should be present";
        expectEqualField(expected, *actualOpt, "sfTradingFee");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAMMDepositTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMDeposit{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAMMDepositTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMDepositBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAMMDepositTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMDepositNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();

    AMMDepositBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAmount());
    EXPECT_FALSE(tx.getAmount().has_value());
    EXPECT_FALSE(tx.hasAmount2());
    EXPECT_FALSE(tx.getAmount2().has_value());
    EXPECT_FALSE(tx.hasEPrice());
    EXPECT_FALSE(tx.getEPrice().has_value());
    EXPECT_FALSE(tx.hasLPTokenOut());
    EXPECT_FALSE(tx.getLPTokenOut().has_value());
    EXPECT_FALSE(tx.hasTradingFee());
    EXPECT_FALSE(tx.getTradingFee().has_value());
}

}
