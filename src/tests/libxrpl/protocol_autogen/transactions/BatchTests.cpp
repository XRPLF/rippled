// Auto-generated unit tests for transaction Batch


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/Batch.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

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
        auto const& expected = rawTransactionsValue;
        auto const actual = tx.getRawTransactions();
        expectEqualField(expected, actual, "sfRawTransactions");
    }

    // Verify optional fields
    {
        auto const& expected = batchSignersValue;
        auto const actualOpt = tx.getBatchSigners();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBatchSigners should be present";
        expectEqualField(expected, *actualOpt, "sfBatchSigners");
        EXPECT_TRUE(tx.hasBatchSigners());
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
    BatchBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = rawTransactionsValue;
        auto const actual = rebuiltTx.getRawTransactions();
        expectEqualField(expected, actual, "sfRawTransactions");
    }

    // Verify optional fields
    {
        auto const& expected = batchSignersValue;
        auto const actualOpt = rebuiltTx.getBatchSigners();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBatchSigners should be present";
        expectEqualField(expected, *actualOpt, "sfBatchSigners");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsBatchTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(Batch{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsBatchTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BatchBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsBatchTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testBatchNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const rawTransactionsValue = canonical_ARRAY();

    BatchBuilder builder{
        accountValue,
        rawTransactionsValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasBatchSigners());
    EXPECT_FALSE(tx.getBatchSigners().has_value());
}

}
