// Auto-generated unit tests for transaction LoanBrokerCoverWithdraw

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LoanBrokerCoverWithdraw.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLoanBrokerCoverWithdrawTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerCoverWithdraw"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();

    LoanBrokerCoverWithdrawBuilder builder{
        accountValue,
        loanBrokerIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestination(destinationValue);
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
        auto const& expected = loanBrokerIDValue;
        auto const actual = tx->getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx->getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx->hasDestination());
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
TEST(TransactionsLoanBrokerCoverWithdrawTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerCoverWithdrawFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();

    // Build an initial transaction
    LoanBrokerCoverWithdrawBuilder initialBuilder{
        accountValue,
        loanBrokerIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestination(destinationValue);
    initialBuilder.setDestinationTag(destinationTagValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LoanBrokerCoverWithdrawBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = loanBrokerIDValue;
        auto const actual = rebuiltTx->getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx->getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx->getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

}

}
