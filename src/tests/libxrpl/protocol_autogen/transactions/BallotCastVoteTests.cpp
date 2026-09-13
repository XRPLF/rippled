// Auto-generated unit tests for transaction BallotCastVote


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/BallotCastVote.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsBallotCastVoteTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotCastVote"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ballotIDValue = canonical_UINT256();
    auto const encryptedVotesValue = canonical_ARRAY();
    auto const auditorEncryptedVotesValue = canonical_ARRAY();
    auto const voterPublicKeyValue = canonical_VL();
    auto const voterEncryptedVotesValue = canonical_ARRAY();
    auto const zKProofValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();
    auto const credentialIDsValue = canonical_VECTOR256();

    BallotCastVoteBuilder builder{
        accountValue,
        ballotIDValue,
        encryptedVotesValue,
        zKProofValue,
        blindingFactorValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAuditorEncryptedVotes(auditorEncryptedVotesValue);
    builder.setVoterPublicKey(voterPublicKeyValue);
    builder.setVoterEncryptedVotes(voterEncryptedVotesValue);
    builder.setCredentialIDs(credentialIDsValue);

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
        auto const& expected = encryptedVotesValue;
        auto const actual = tx.getEncryptedVotes();
        expectEqualField(expected, actual, "sfEncryptedVotes");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    {
        auto const& expected = blindingFactorValue;
        auto const actual = tx.getBlindingFactor();
        expectEqualField(expected, actual, "sfBlindingFactor");
    }

    // Verify optional fields
    {
        auto const& expected = auditorEncryptedVotesValue;
        auto const actualOpt = tx.getAuditorEncryptedVotes();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedVotes should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedVotes");
        EXPECT_TRUE(tx.hasAuditorEncryptedVotes());
    }

    {
        auto const& expected = voterPublicKeyValue;
        auto const actualOpt = tx.getVoterPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfVoterPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfVoterPublicKey");
        EXPECT_TRUE(tx.hasVoterPublicKey());
    }

    {
        auto const& expected = voterEncryptedVotesValue;
        auto const actualOpt = tx.getVoterEncryptedVotes();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfVoterEncryptedVotes should be present";
        expectEqualField(expected, *actualOpt, "sfVoterEncryptedVotes");
        EXPECT_TRUE(tx.hasVoterEncryptedVotes());
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = tx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
        EXPECT_TRUE(tx.hasCredentialIDs());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsBallotCastVoteTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotCastVoteFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ballotIDValue = canonical_UINT256();
    auto const encryptedVotesValue = canonical_ARRAY();
    auto const auditorEncryptedVotesValue = canonical_ARRAY();
    auto const voterPublicKeyValue = canonical_VL();
    auto const voterEncryptedVotesValue = canonical_ARRAY();
    auto const zKProofValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();
    auto const credentialIDsValue = canonical_VECTOR256();

    // Build an initial transaction
    BallotCastVoteBuilder initialBuilder{
        accountValue,
        ballotIDValue,
        encryptedVotesValue,
        zKProofValue,
        blindingFactorValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAuditorEncryptedVotes(auditorEncryptedVotesValue);
    initialBuilder.setVoterPublicKey(voterPublicKeyValue);
    initialBuilder.setVoterEncryptedVotes(voterEncryptedVotesValue);
    initialBuilder.setCredentialIDs(credentialIDsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    BallotCastVoteBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = encryptedVotesValue;
        auto const actual = rebuiltTx.getEncryptedVotes();
        expectEqualField(expected, actual, "sfEncryptedVotes");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    {
        auto const& expected = blindingFactorValue;
        auto const actual = rebuiltTx.getBlindingFactor();
        expectEqualField(expected, actual, "sfBlindingFactor");
    }

    // Verify optional fields
    {
        auto const& expected = auditorEncryptedVotesValue;
        auto const actualOpt = rebuiltTx.getAuditorEncryptedVotes();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedVotes should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedVotes");
    }

    {
        auto const& expected = voterPublicKeyValue;
        auto const actualOpt = rebuiltTx.getVoterPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfVoterPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfVoterPublicKey");
    }

    {
        auto const& expected = voterEncryptedVotesValue;
        auto const actualOpt = rebuiltTx.getVoterEncryptedVotes();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfVoterEncryptedVotes should be present";
        expectEqualField(expected, *actualOpt, "sfVoterEncryptedVotes");
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = rebuiltTx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsBallotCastVoteTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BallotCastVote{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsBallotCastVoteTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BallotCastVoteBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsBallotCastVoteTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotCastVoteNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const ballotIDValue = canonical_UINT256();
    auto const encryptedVotesValue = canonical_ARRAY();
    auto const zKProofValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();

    BallotCastVoteBuilder builder{
        accountValue,
        ballotIDValue,
        encryptedVotesValue,
        zKProofValue,
        blindingFactorValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAuditorEncryptedVotes());
    EXPECT_FALSE(tx.getAuditorEncryptedVotes().has_value());
    EXPECT_FALSE(tx.hasVoterPublicKey());
    EXPECT_FALSE(tx.getVoterPublicKey().has_value());
    EXPECT_FALSE(tx.hasVoterEncryptedVotes());
    EXPECT_FALSE(tx.getVoterEncryptedVotes().has_value());
    EXPECT_FALSE(tx.hasCredentialIDs());
    EXPECT_FALSE(tx.getCredentialIDs().has_value());
}

}
