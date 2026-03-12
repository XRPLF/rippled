// Auto-generated unit tests for transaction XChainCommit


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/XChainCommit.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsXChainCommitTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainCommit"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const amountValue = canonical_AMOUNT();
    auto const otherChainDestinationValue = canonical_ACCOUNT();

    XChainCommitBuilder builder{
        accountValue,
        xChainBridgeValue,
        xChainClaimIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setOtherChainDestination(otherChainDestinationValue);

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
        auto const& expected = xChainClaimIDValue;
        auto const actual = tx.getXChainClaimID();
        expectEqualField(expected, actual, "sfXChainClaimID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = otherChainDestinationValue;
        auto const actualOpt = tx.getOtherChainDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOtherChainDestination should be present";
        expectEqualField(expected, *actualOpt, "sfOtherChainDestination");
        EXPECT_TRUE(tx.hasOtherChainDestination());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsXChainCommitTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainCommitFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const amountValue = canonical_AMOUNT();
    auto const otherChainDestinationValue = canonical_ACCOUNT();

    // Build an initial transaction
    XChainCommitBuilder initialBuilder{
        accountValue,
        xChainBridgeValue,
        xChainClaimIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setOtherChainDestination(otherChainDestinationValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    XChainCommitBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = xChainClaimIDValue;
        auto const actual = rebuiltTx.getXChainClaimID();
        expectEqualField(expected, actual, "sfXChainClaimID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = otherChainDestinationValue;
        auto const actualOpt = rebuiltTx.getOtherChainDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOtherChainDestination should be present";
        expectEqualField(expected, *actualOpt, "sfOtherChainDestination");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsXChainCommitTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainCommit{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsXChainCommitTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainCommitBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsXChainCommitTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainCommitNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const amountValue = canonical_AMOUNT();

    XChainCommitBuilder builder{
        accountValue,
        xChainBridgeValue,
        xChainClaimIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasOtherChainDestination());
    EXPECT_FALSE(tx.getOtherChainDestination().has_value());
}

}
