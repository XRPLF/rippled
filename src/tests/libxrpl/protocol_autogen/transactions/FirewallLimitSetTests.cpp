// Auto-generated unit tests for transaction FirewallLimitSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/FirewallLimitSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsFirewallLimitSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallLimitSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const limitAmountValue = canonical_AMOUNT();
    auto const periodValue = canonical_UINT32();
    auto const counterpartySignatureValue = canonical_OBJECT();

    FirewallLimitSetBuilder builder{
        accountValue,
        assetValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setLimitAmount(limitAmountValue);
    builder.setPeriod(periodValue);
    builder.setCounterpartySignature(counterpartySignatureValue);

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

    // Verify optional fields
    {
        auto const& expected = limitAmountValue;
        auto const actualOpt = tx.getLimitAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLimitAmount should be present";
        expectEqualField(expected, *actualOpt, "sfLimitAmount");
        EXPECT_TRUE(tx.hasLimitAmount());
    }

    {
        auto const& expected = periodValue;
        auto const actualOpt = tx.getPeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPeriod should be present";
        expectEqualField(expected, *actualOpt, "sfPeriod");
        EXPECT_TRUE(tx.hasPeriod());
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = tx.getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
        EXPECT_TRUE(tx.hasCounterpartySignature());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsFirewallLimitSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallLimitSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const limitAmountValue = canonical_AMOUNT();
    auto const periodValue = canonical_UINT32();
    auto const counterpartySignatureValue = canonical_OBJECT();

    // Build an initial transaction
    FirewallLimitSetBuilder initialBuilder{
        accountValue,
        assetValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setLimitAmount(limitAmountValue);
    initialBuilder.setPeriod(periodValue);
    initialBuilder.setCounterpartySignature(counterpartySignatureValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    FirewallLimitSetBuilder builderFromTx{initialTx.getSTTx()};

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

    // Verify optional fields
    {
        auto const& expected = limitAmountValue;
        auto const actualOpt = rebuiltTx.getLimitAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLimitAmount should be present";
        expectEqualField(expected, *actualOpt, "sfLimitAmount");
    }

    {
        auto const& expected = periodValue;
        auto const actualOpt = rebuiltTx.getPeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPeriod should be present";
        expectEqualField(expected, *actualOpt, "sfPeriod");
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = rebuiltTx.getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsFirewallLimitSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(FirewallLimitSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsFirewallLimitSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(FirewallLimitSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsFirewallLimitSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallLimitSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();

    FirewallLimitSetBuilder builder{
        accountValue,
        assetValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasLimitAmount());
    EXPECT_FALSE(tx.getLimitAmount().has_value());
    EXPECT_FALSE(tx.hasPeriod());
    EXPECT_FALSE(tx.getPeriod().has_value());
    EXPECT_FALSE(tx.hasCounterpartySignature());
    EXPECT_FALSE(tx.getCounterpartySignature().has_value());
}

}
