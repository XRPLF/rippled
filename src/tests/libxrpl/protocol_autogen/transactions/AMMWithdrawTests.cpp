// Auto-generated unit tests for transaction AMMWithdraw


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMWithdraw.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAMMWithdrawTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMWithdraw"));

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
    auto const lPTokenInValue = canonical_AMOUNT();

    AMMWithdrawBuilder builder{
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
    builder.setLPTokenIn(lPTokenInValue);

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
        auto const& expected = lPTokenInValue;
        auto const actualOpt = tx.getLPTokenIn();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLPTokenIn should be present";
        expectEqualField(expected, *actualOpt, "sfLPTokenIn");
        EXPECT_TRUE(tx.hasLPTokenIn());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAMMWithdrawTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMWithdrawFromTx"));

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
    auto const lPTokenInValue = canonical_AMOUNT();

    // Build an initial transaction
    AMMWithdrawBuilder initialBuilder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAmount(amountValue);
    initialBuilder.setAmount2(amount2Value);
    initialBuilder.setEPrice(ePriceValue);
    initialBuilder.setLPTokenIn(lPTokenInValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AMMWithdrawBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = lPTokenInValue;
        auto const actualOpt = rebuiltTx.getLPTokenIn();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLPTokenIn should be present";
        expectEqualField(expected, *actualOpt, "sfLPTokenIn");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAMMWithdrawTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMWithdraw{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAMMWithdrawTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMWithdrawBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAMMWithdrawTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMWithdrawNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();

    AMMWithdrawBuilder builder{
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
    EXPECT_FALSE(tx.hasLPTokenIn());
    EXPECT_FALSE(tx.getLPTokenIn().has_value());
}

}
