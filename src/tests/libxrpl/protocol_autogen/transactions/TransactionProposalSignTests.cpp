// Auto-generated unit tests for transaction TransactionProposalSign


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/TransactionProposalSign.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsTransactionProposalSignTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTransactionProposalSign"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const proposalIDValue = canonical_UINT256();
    auto const signingForValue = canonical_ACCOUNT();
    auto const proposalSignatureValue = canonical_OBJECT();

    TransactionProposalSignBuilder builder{
        accountValue,
        proposalIDValue,
        signingForValue,
        proposalSignatureValue,
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
        auto const& expected = proposalIDValue;
        auto const actual = tx.getProposalID();
        expectEqualField(expected, actual, "sfProposalID");
    }

    {
        auto const& expected = signingForValue;
        auto const actual = tx.getSigningFor();
        expectEqualField(expected, actual, "sfSigningFor");
    }

    {
        auto const& expected = proposalSignatureValue;
        auto const actual = tx.getProposalSignature();
        expectEqualField(expected, actual, "sfProposalSignature");
    }

    // Verify optional fields
}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsTransactionProposalSignTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTransactionProposalSignFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const proposalIDValue = canonical_UINT256();
    auto const signingForValue = canonical_ACCOUNT();
    auto const proposalSignatureValue = canonical_OBJECT();

    // Build an initial transaction
    TransactionProposalSignBuilder initialBuilder{
        accountValue,
        proposalIDValue,
        signingForValue,
        proposalSignatureValue,
        sequenceValue,
        feeValue
    };


    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    TransactionProposalSignBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = proposalIDValue;
        auto const actual = rebuiltTx.getProposalID();
        expectEqualField(expected, actual, "sfProposalID");
    }

    {
        auto const& expected = signingForValue;
        auto const actual = rebuiltTx.getSigningFor();
        expectEqualField(expected, actual, "sfSigningFor");
    }

    {
        auto const& expected = proposalSignatureValue;
        auto const actual = rebuiltTx.getProposalSignature();
        expectEqualField(expected, actual, "sfProposalSignature");
    }

    // Verify optional fields
}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsTransactionProposalSignTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TransactionProposalSign{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsTransactionProposalSignTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TransactionProposalSignBuilder{wrongTx.getSTTx()}, std::runtime_error);
}


}
