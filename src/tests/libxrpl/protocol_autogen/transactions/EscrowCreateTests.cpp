// Auto-generated unit tests for transaction EscrowCreate

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsEscrowCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testEscrowCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const conditionValue = canonical_VL();
    auto const cancelAfterValue = canonical_UINT32();
    auto const finishAfterValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();

    EscrowCreateBuilder builder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCondition(conditionValue);
    builder.setCancelAfter(cancelAfterValue);
    builder.setFinishAfter(finishAfterValue);
    builder.setDestinationTag(destinationTagValue);

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
        auto const& expected = destinationValue;
        auto const actual = tx->getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = conditionValue;
        auto const actualOpt = tx->getCondition();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCondition should be present";
        expectEqualField(expected, *actualOpt, "sfCondition");
        EXPECT_TRUE(tx->hasCondition());
    }

    {
        auto const& expected = cancelAfterValue;
        auto const actualOpt = tx->getCancelAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCancelAfter should be present";
        expectEqualField(expected, *actualOpt, "sfCancelAfter");
        EXPECT_TRUE(tx->hasCancelAfter());
    }

    {
        auto const& expected = finishAfterValue;
        auto const actualOpt = tx->getFinishAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFinishAfter should be present";
        expectEqualField(expected, *actualOpt, "sfFinishAfter");
        EXPECT_TRUE(tx->hasFinishAfter());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx->getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx->hasDestinationTag());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsEscrowCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testEscrowCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const conditionValue = canonical_VL();
    auto const cancelAfterValue = canonical_UINT32();
    auto const finishAfterValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();

    // Build an initial transaction
    EscrowCreateBuilder initialBuilder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCondition(conditionValue);
    initialBuilder.setCancelAfter(cancelAfterValue);
    initialBuilder.setFinishAfter(finishAfterValue);
    initialBuilder.setDestinationTag(destinationTagValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    EscrowCreateBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx->getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = conditionValue;
        auto const actualOpt = rebuiltTx->getCondition();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCondition should be present";
        expectEqualField(expected, *actualOpt, "sfCondition");
    }

    {
        auto const& expected = cancelAfterValue;
        auto const actualOpt = rebuiltTx->getCancelAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCancelAfter should be present";
        expectEqualField(expected, *actualOpt, "sfCancelAfter");
    }

    {
        auto const& expected = finishAfterValue;
        auto const actualOpt = rebuiltTx->getFinishAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFinishAfter should be present";
        expectEqualField(expected, *actualOpt, "sfFinishAfter");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx->getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

}

}
