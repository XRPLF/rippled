// Auto-generated unit tests for transaction FirewallDelete


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/FirewallDelete.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsFirewallDeleteTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallDelete"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    FirewallDeleteBuilder builder{
        accountValue,
        counterpartySignatureValue,
        firewallIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields

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
        auto const& expected = counterpartySignatureValue;
        auto const actual = tx.getCounterpartySignature();
        expectEqualField(expected, actual, "sfCounterpartySignature");
    }

    {
        auto const& expected = firewallIDValue;
        auto const actual = tx.getFirewallID();
        expectEqualField(expected, actual, "sfFirewallID");
    }

    // Verify optional fields
}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsFirewallDeleteTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testFirewallDeleteFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    // Build an initial transaction
    FirewallDeleteBuilder initialBuilder{
        accountValue,
        counterpartySignatureValue,
        firewallIDValue,
        sequenceValue,
        feeValue
    };


    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    FirewallDeleteBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = counterpartySignatureValue;
        auto const actual = rebuiltTx.getCounterpartySignature();
        expectEqualField(expected, actual, "sfCounterpartySignature");
    }

    {
        auto const& expected = firewallIDValue;
        auto const actual = rebuiltTx.getFirewallID();
        expectEqualField(expected, actual, "sfFirewallID");
    }

    // Verify optional fields
}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsFirewallDeleteTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(FirewallDelete{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsFirewallDeleteTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(FirewallDeleteBuilder{wrongTx.getSTTx()}, std::runtime_error);
}


}
