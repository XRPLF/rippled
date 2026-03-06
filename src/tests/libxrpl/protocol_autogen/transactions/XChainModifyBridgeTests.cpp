// Auto-generated unit tests for transaction XChainModifyBridge

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/XChainModifyBridge.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsXChainModifyBridgeTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainModifyBridge"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const signatureRewardValue = canonical_AMOUNT();
    auto const minAccountCreateAmountValue = canonical_AMOUNT();

    XChainModifyBridgeBuilder builder{
        accountValue,
        xChainBridgeValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setSignatureReward(signatureRewardValue);
    builder.setMinAccountCreateAmount(minAccountCreateAmountValue);

    std::string reason;
    EXPECT_TRUE(builder.validate(reason)) << reason;

    auto tx = builder.build(publicKey, secretKey);

    EXPECT_TRUE(tx->validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx->getSigningPubKey().empty());
    EXPECT_TRUE(tx->hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx->getAccount(), accountValue);
    EXPECT_EQ(tx->getSequence(), sequenceValue);
    EXPECT_EQ(tx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = xChainBridgeValue;
        auto const actual = tx->getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    // Verify optional fields
    {
        auto const& expected = signatureRewardValue;
        auto const actualOpt = tx->getSignatureReward();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignatureReward should be present";
        expectEqualField(expected, *actualOpt, "sfSignatureReward");
        EXPECT_TRUE(tx->hasSignatureReward());
    }

    {
        auto const& expected = minAccountCreateAmountValue;
        auto const actualOpt = tx->getMinAccountCreateAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMinAccountCreateAmount should be present";
        expectEqualField(expected, *actualOpt, "sfMinAccountCreateAmount");
        EXPECT_TRUE(tx->hasMinAccountCreateAmount());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsXChainModifyBridgeTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainModifyBridgeFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const signatureRewardValue = canonical_AMOUNT();
    auto const minAccountCreateAmountValue = canonical_AMOUNT();

    // Build an initial transaction
    XChainModifyBridgeBuilder initialBuilder{
        accountValue,
        xChainBridgeValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setSignatureReward(signatureRewardValue);
    initialBuilder.setMinAccountCreateAmount(minAccountCreateAmountValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    XChainModifyBridgeBuilder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = xChainBridgeValue;
        auto const actual = rebuiltTx->getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    // Verify optional fields
    {
        auto const& expected = signatureRewardValue;
        auto const actualOpt = rebuiltTx->getSignatureReward();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignatureReward should be present";
        expectEqualField(expected, *actualOpt, "sfSignatureReward");
    }

    {
        auto const& expected = minAccountCreateAmountValue;
        auto const actualOpt = rebuiltTx->getMinAccountCreateAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMinAccountCreateAmount should be present";
        expectEqualField(expected, *actualOpt, "sfMinAccountCreateAmount");
    }

}

}
