// Auto-generated unit tests for transaction LoanBrokerCoverClawback

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LoanBrokerCoverClawback.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLoanBrokerCoverClawbackTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerCoverClawback"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();

    LoanBrokerCoverClawbackBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setLoanBrokerID(loanBrokerIDValue);
    builder.setAmount(amountValue);

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
    // Verify optional fields
    {
        auto const& expected = loanBrokerIDValue;
        auto const actualOpt = tx->getLoanBrokerID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanBrokerID should be present";
        expectEqualField(expected, *actualOpt, "sfLoanBrokerID");
        EXPECT_TRUE(tx->hasLoanBrokerID());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = tx->getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(tx->hasAmount());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsLoanBrokerCoverClawbackTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerCoverClawbackFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();

    // Build an initial transaction
    LoanBrokerCoverClawbackBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setLoanBrokerID(loanBrokerIDValue);
    initialBuilder.setAmount(amountValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LoanBrokerCoverClawbackBuilder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
    // Verify optional fields
    {
        auto const& expected = loanBrokerIDValue;
        auto const actualOpt = rebuiltTx->getLoanBrokerID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanBrokerID should be present";
        expectEqualField(expected, *actualOpt, "sfLoanBrokerID");
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx->getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

}

}
