// Auto-generated unit tests for transaction Batch

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/Batch.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsBatchTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testBatch"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const rawTransactionsValue = canonical_ARRAY();
    auto const batchSignersValue = canonical_ARRAY();

    BatchBuilder builder{
        accountValue,
        rawTransactionsValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setBatchSigners(batchSignersValue);

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
        auto const& expected = rawTransactionsValue;
        auto const actual = tx->getRawTransactions();
        expectEqualField(expected, actual, "sfRawTransactions");
    }

    // Verify optional fields
    {
        auto const& expected = batchSignersValue;
        auto const actualOpt = tx->getBatchSigners();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBatchSigners should be present";
        expectEqualField(expected, *actualOpt, "sfBatchSigners");
        EXPECT_TRUE(tx->hasBatchSigners());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsBatchTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testBatchFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const rawTransactionsValue = canonical_ARRAY();
    auto const batchSignersValue = canonical_ARRAY();

    // Build an initial transaction
    BatchBuilder initialBuilder{
        accountValue,
        rawTransactionsValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setBatchSigners(batchSignersValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    BatchBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = rawTransactionsValue;
        auto const actual = rebuiltTx->getRawTransactions();
        expectEqualField(expected, actual, "sfRawTransactions");
    }

    // Verify optional fields
    {
        auto const& expected = batchSignersValue;
        auto const actualOpt = rebuiltTx->getBatchSigners();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBatchSigners should be present";
        expectEqualField(expected, *actualOpt, "sfBatchSigners");
    }

}

}
