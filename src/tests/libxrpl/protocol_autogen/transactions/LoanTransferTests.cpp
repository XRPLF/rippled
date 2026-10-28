// Auto-generated unit tests for transaction LoanTransfer


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LoanTransfer.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLoanTransferTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testLoanTransfer"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const counterpartySignatureValue = canonical_OBJECT();

    LoanTransferBuilder builder{
        accountValue,
        loanIDValue,
        loanBrokerIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCounterparty(counterpartyValue);
    builder.setCounterpartySignature(counterpartySignatureValue);

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
        auto const& expected = loanIDValue;
        auto const actual = tx.getLoanID();
        expectEqualField(expected, actual, "sfLoanID");
    }

    {
        auto const& expected = loanBrokerIDValue;
        auto const actual = tx.getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    // Verify optional fields
    {
        auto const& expected = counterpartyValue;
        auto const actualOpt = tx.getCounterparty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterparty should be present";
        expectEqualField(expected, *actualOpt, "sfCounterparty");
        EXPECT_TRUE(tx.hasCounterparty());
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = tx.getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
        EXPECT_TRUE(tx.hasCounterpartySignature());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsLoanTransferTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testLoanTransferFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const counterpartySignatureValue = canonical_OBJECT();

    // Build an initial transaction
    LoanTransferBuilder initialBuilder{
        accountValue,
        loanIDValue,
        loanBrokerIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCounterparty(counterpartyValue);
    initialBuilder.setCounterpartySignature(counterpartySignatureValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LoanTransferBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = loanIDValue;
        auto const actual = rebuiltTx.getLoanID();
        expectEqualField(expected, actual, "sfLoanID");
    }

    {
        auto const& expected = loanBrokerIDValue;
        auto const actual = rebuiltTx.getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    // Verify optional fields
    {
        auto const& expected = counterpartyValue;
        auto const actualOpt = rebuiltTx.getCounterparty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterparty should be present";
        expectEqualField(expected, *actualOpt, "sfCounterparty");
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = rebuiltTx.getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsLoanTransferTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(LoanTransfer{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsLoanTransferTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(LoanTransferBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsLoanTransferTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testLoanTransferNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const loanIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();

    LoanTransferBuilder builder{
        accountValue,
        loanIDValue,
        loanBrokerIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCounterparty());
    EXPECT_FALSE(tx.getCounterparty().has_value());
    EXPECT_FALSE(tx.hasCounterpartySignature());
    EXPECT_FALSE(tx.getCounterpartySignature().has_value());
}

}
