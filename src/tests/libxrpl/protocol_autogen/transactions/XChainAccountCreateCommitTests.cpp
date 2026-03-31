// Auto-generated unit tests for transaction XChainAccountCreateCommit


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/XChainAccountCreateCommit.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsXChainAccountCreateCommitTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAccountCreateCommit"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const signatureRewardValue = canonical_AMOUNT();

    XChainAccountCreateCommitBuilder builder{
        accountValue,
        xChainBridgeValue,
        destinationValue,
        amountValue,
        signatureRewardValue,
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
        auto const& expected = xChainBridgeValue;
        auto const actual = tx.getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    {
        auto const& expected = destinationValue;
        auto const actual = tx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = signatureRewardValue;
        auto const actual = tx.getSignatureReward();
        expectEqualField(expected, actual, "sfSignatureReward");
    }

    // Verify optional fields
}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsXChainAccountCreateCommitTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAccountCreateCommitFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const signatureRewardValue = canonical_AMOUNT();

    // Build an initial transaction
    XChainAccountCreateCommitBuilder initialBuilder{
        accountValue,
        xChainBridgeValue,
        destinationValue,
        amountValue,
        signatureRewardValue,
        sequenceValue,
        feeValue
    };


    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    XChainAccountCreateCommitBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = xChainBridgeValue;
        auto const actual = rebuiltTx.getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    {
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = signatureRewardValue;
        auto const actual = rebuiltTx.getSignatureReward();
        expectEqualField(expected, actual, "sfSignatureReward");
    }

    // Verify optional fields
}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsXChainAccountCreateCommitTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainAccountCreateCommit{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsXChainAccountCreateCommitTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainAccountCreateCommitBuilder{wrongTx.getSTTx()}, std::runtime_error);
}


}
