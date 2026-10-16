// Auto-generated unit tests for ledger entry BallotVote


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/BallotVote.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(BallotVoteTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const ballotIDValue = canonical_UINT256();
    auto const ballotWeightValue = canonical_UINT64();
    auto const encryptedVotesValue = canonical_ARRAY();
    auto const auditorEncryptedVotesValue = canonical_ARRAY();
    auto const voterPublicKeyValue = canonical_VL();
    auto const voterEncryptedVotesValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    BallotVoteBuilder builder{
        accountValue,
        ballotIDValue,
        ballotWeightValue,
        encryptedVotesValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setAuditorEncryptedVotes(auditorEncryptedVotesValue);
    builder.setVoterPublicKey(voterPublicKeyValue);
    builder.setVoterEncryptedVotes(voterEncryptedVotesValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = ballotIDValue;
        auto const actual = entry.getBallotID();
        expectEqualField(expected, actual, "sfBallotID");
    }

    {
        auto const& expected = ballotWeightValue;
        auto const actual = entry.getBallotWeight();
        expectEqualField(expected, actual, "sfBallotWeight");
    }

    {
        auto const& expected = encryptedVotesValue;
        auto const actual = entry.getEncryptedVotes();
        expectEqualField(expected, actual, "sfEncryptedVotes");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = previousTxnIDValue;
        auto const actual = entry.getPreviousTxnID();
        expectEqualField(expected, actual, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;
        auto const actual = entry.getPreviousTxnLgrSeq();
        expectEqualField(expected, actual, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = auditorEncryptedVotesValue;
        auto const actualOpt = entry.getAuditorEncryptedVotes();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedVotes");
        EXPECT_TRUE(entry.hasAuditorEncryptedVotes());
    }

    {
        auto const& expected = voterPublicKeyValue;
        auto const actualOpt = entry.getVoterPublicKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVoterPublicKey");
        EXPECT_TRUE(entry.hasVoterPublicKey());
    }

    {
        auto const& expected = voterEncryptedVotesValue;
        auto const actualOpt = entry.getVoterEncryptedVotes();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVoterEncryptedVotes");
        EXPECT_TRUE(entry.hasVoterEncryptedVotes());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(BallotVoteTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const ballotIDValue = canonical_UINT256();
    auto const ballotWeightValue = canonical_UINT64();
    auto const encryptedVotesValue = canonical_ARRAY();
    auto const auditorEncryptedVotesValue = canonical_ARRAY();
    auto const voterPublicKeyValue = canonical_VL();
    auto const voterEncryptedVotesValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(BallotVote::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfBallotID) = ballotIDValue;
    sle->at(sfBallotWeight) = ballotWeightValue;
    sle->setFieldArray(sfEncryptedVotes, encryptedVotesValue);
    sle->setFieldArray(sfAuditorEncryptedVotes, auditorEncryptedVotesValue);
    sle->at(sfVoterPublicKey) = voterPublicKeyValue;
    sle->setFieldArray(sfVoterEncryptedVotes, voterEncryptedVotesValue);
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    BallotVoteBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    BallotVote entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = ballotIDValue;

        auto const fromSle = entryFromSle.getBallotID();
        auto const fromBuilder = entryFromBuilder.getBallotID();

        expectEqualField(expected, fromSle, "sfBallotID");
        expectEqualField(expected, fromBuilder, "sfBallotID");
    }

    {
        auto const& expected = ballotWeightValue;

        auto const fromSle = entryFromSle.getBallotWeight();
        auto const fromBuilder = entryFromBuilder.getBallotWeight();

        expectEqualField(expected, fromSle, "sfBallotWeight");
        expectEqualField(expected, fromBuilder, "sfBallotWeight");
    }

    {
        auto const& expected = encryptedVotesValue;

        auto const fromSle = entryFromSle.getEncryptedVotes();
        auto const fromBuilder = entryFromBuilder.getEncryptedVotes();

        expectEqualField(expected, fromSle, "sfEncryptedVotes");
        expectEqualField(expected, fromBuilder, "sfEncryptedVotes");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = previousTxnIDValue;

        auto const fromSle = entryFromSle.getPreviousTxnID();
        auto const fromBuilder = entryFromBuilder.getPreviousTxnID();

        expectEqualField(expected, fromSle, "sfPreviousTxnID");
        expectEqualField(expected, fromBuilder, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;

        auto const fromSle = entryFromSle.getPreviousTxnLgrSeq();
        auto const fromBuilder = entryFromBuilder.getPreviousTxnLgrSeq();

        expectEqualField(expected, fromSle, "sfPreviousTxnLgrSeq");
        expectEqualField(expected, fromBuilder, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = auditorEncryptedVotesValue;

        auto const fromSleOpt = entryFromSle.getAuditorEncryptedVotes();
        auto const fromBuilderOpt = entryFromBuilder.getAuditorEncryptedVotes();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuditorEncryptedVotes");
        expectEqualField(expected, *fromBuilderOpt, "sfAuditorEncryptedVotes");
    }

    {
        auto const& expected = voterPublicKeyValue;

        auto const fromSleOpt = entryFromSle.getVoterPublicKey();
        auto const fromBuilderOpt = entryFromBuilder.getVoterPublicKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVoterPublicKey");
        expectEqualField(expected, *fromBuilderOpt, "sfVoterPublicKey");
    }

    {
        auto const& expected = voterEncryptedVotesValue;

        auto const fromSleOpt = entryFromSle.getVoterEncryptedVotes();
        auto const fromBuilderOpt = entryFromBuilder.getVoterEncryptedVotes();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVoterEncryptedVotes");
        expectEqualField(expected, *fromBuilderOpt, "sfVoterEncryptedVotes");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(BallotVoteTests, WrapperThrowsOnWrongEntryType)
{
    uint256 const index{3u};

    // Build a valid ledger entry of a different type
    // Ticket requires: Account, OwnerNode, TicketSequence, PreviousTxnID, PreviousTxnLgrSeq
    // Check requires: Account, Destination, SendMax, Sequence, OwnerNode, DestinationNode, PreviousTxnID, PreviousTxnLgrSeq
    TicketBuilder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(BallotVote{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(BallotVoteTests, BuilderThrowsOnWrongEntryType)
{
    uint256 const index{4u};

    // Build a valid ledger entry of a different type
    TicketBuilder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(BallotVoteBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(BallotVoteTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const accountValue = canonical_ACCOUNT();
    auto const ballotIDValue = canonical_UINT256();
    auto const ballotWeightValue = canonical_UINT64();
    auto const encryptedVotesValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    BallotVoteBuilder builder{
        accountValue,
        ballotIDValue,
        ballotWeightValue,
        encryptedVotesValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAuditorEncryptedVotes());
    EXPECT_FALSE(entry.getAuditorEncryptedVotes().has_value());
    EXPECT_FALSE(entry.hasVoterPublicKey());
    EXPECT_FALSE(entry.getVoterPublicKey().has_value());
    EXPECT_FALSE(entry.hasVoterEncryptedVotes());
    EXPECT_FALSE(entry.getVoterEncryptedVotes().has_value());
}
}
