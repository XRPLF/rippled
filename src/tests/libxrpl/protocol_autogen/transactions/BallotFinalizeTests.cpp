// Auto-generated unit tests for transaction BallotFinalize


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/BallotFinalize.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsBallotFinalizeTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotFinalize"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ballotIDValue = canonical_UINT256();
    auto const resultsValue = canonical_ARRAY();
    auto const zKProofValue = canonical_VL();

    BallotFinalizeBuilder builder{
        accountValue,
        ballotIDValue,
        resultsValue,
        zKProofValue,
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
        auto const& expected = ballotIDValue;
        auto const actual = tx.getBallotID();
        expectEqualField(expected, actual, "sfBallotID");
    }

    {
        auto const& expected = resultsValue;
        auto const actual = tx.getResults();
        expectEqualField(expected, actual, "sfResults");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsBallotFinalizeTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotFinalizeFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ballotIDValue = canonical_UINT256();
    auto const resultsValue = canonical_ARRAY();
    auto const zKProofValue = canonical_VL();

    // Build an initial transaction
    BallotFinalizeBuilder initialBuilder{
        accountValue,
        ballotIDValue,
        resultsValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };


    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    BallotFinalizeBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = ballotIDValue;
        auto const actual = rebuiltTx.getBallotID();
        expectEqualField(expected, actual, "sfBallotID");
    }

    {
        auto const& expected = resultsValue;
        auto const actual = rebuiltTx.getResults();
        expectEqualField(expected, actual, "sfResults");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsBallotFinalizeTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BallotFinalize{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsBallotFinalizeTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BallotFinalizeBuilder{wrongTx.getSTTx()}, std::runtime_error);
}


}
