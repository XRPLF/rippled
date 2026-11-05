// Auto-generated unit tests for transaction FirewallSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/FirewallSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsFirewallSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const backupValue = canonical_ACCOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    FirewallSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCounterparty(counterpartyValue);
    builder.setBackup(backupValue);
    builder.setMaxFee(maxFeeValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setCounterpartySignature(counterpartySignatureValue);
    builder.setFirewallID(firewallIDValue);

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
        auto const& expected = counterpartyValue;
        auto const actualOpt = tx.getCounterparty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterparty should be present";
        expectEqualField(expected, *actualOpt, "sfCounterparty");
        EXPECT_TRUE(tx.hasCounterparty());
    }

    {
        auto const& expected = backupValue;
        auto const actualOpt = tx.getBackup();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBackup should be present";
        expectEqualField(expected, *actualOpt, "sfBackup");
        EXPECT_TRUE(tx.hasBackup());
    }

    {
        auto const& expected = maxFeeValue;
        auto const actualOpt = tx.getMaxFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaxFee should be present";
        expectEqualField(expected, *actualOpt, "sfMaxFee");
        EXPECT_TRUE(tx.hasMaxFee());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = tx.getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
        EXPECT_TRUE(tx.hasCounterpartySignature());
    }

    {
        auto const& expected = firewallIDValue;
        auto const actualOpt = tx.getFirewallID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFirewallID should be present";
        expectEqualField(expected, *actualOpt, "sfFirewallID");
        EXPECT_TRUE(tx.hasFirewallID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsFirewallSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const backupValue = canonical_ACCOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    // Build an initial transaction
    FirewallSetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCounterparty(counterpartyValue);
    initialBuilder.setBackup(backupValue);
    initialBuilder.setMaxFee(maxFeeValue);
    initialBuilder.setDestinationTag(destinationTagValue);
    initialBuilder.setCounterpartySignature(counterpartySignatureValue);
    initialBuilder.setFirewallID(firewallIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    FirewallSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = counterpartyValue;
        auto const actualOpt = rebuiltTx.getCounterparty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterparty should be present";
        expectEqualField(expected, *actualOpt, "sfCounterparty");
    }

    {
        auto const& expected = backupValue;
        auto const actualOpt = rebuiltTx.getBackup();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBackup should be present";
        expectEqualField(expected, *actualOpt, "sfBackup");
    }

    {
        auto const& expected = maxFeeValue;
        auto const actualOpt = rebuiltTx.getMaxFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaxFee should be present";
        expectEqualField(expected, *actualOpt, "sfMaxFee");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = rebuiltTx.getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
    }

    {
        auto const& expected = firewallIDValue;
        auto const actualOpt = rebuiltTx.getFirewallID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFirewallID should be present";
        expectEqualField(expected, *actualOpt, "sfFirewallID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsFirewallSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(FirewallSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsFirewallSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(FirewallSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsFirewallSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    FirewallSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCounterparty());
    EXPECT_FALSE(tx.getCounterparty().has_value());
    EXPECT_FALSE(tx.hasBackup());
    EXPECT_FALSE(tx.getBackup().has_value());
    EXPECT_FALSE(tx.hasMaxFee());
    EXPECT_FALSE(tx.getMaxFee().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
    EXPECT_FALSE(tx.hasCounterpartySignature());
    EXPECT_FALSE(tx.getCounterpartySignature().has_value());
    EXPECT_FALSE(tx.hasFirewallID());
    EXPECT_FALSE(tx.getFirewallID().has_value());
}

}
