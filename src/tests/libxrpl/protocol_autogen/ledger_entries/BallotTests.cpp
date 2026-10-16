// Auto-generated unit tests for ledger entry Ballot


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Ballot.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(BallotTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const domainIDValue = canonical_UINT256();
    auto const digestValue = canonical_UINT256();
    auto const uRIValue = canonical_VL();
    auto const optionCountValue = canonical_UINT8();
    auto const tallyPublicKeyValue = canonical_VL();
    auto const auditorEncryptionKeyValue = canonical_VL();
    auto const encryptedTallyValue = canonical_ARRAY();
    auto const openTimeValue = canonical_UINT32();
    auto const closeTimeValue = canonical_UINT32();
    auto const voteCountValue = canonical_UINT32();
    auto const resultsValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    BallotBuilder builder{
        ownerValue,
        sequenceValue,
        digestValue,
        optionCountValue,
        tallyPublicKeyValue,
        encryptedTallyValue,
        openTimeValue,
        closeTimeValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    builder.setDomainID(domainIDValue);
    builder.setURI(uRIValue);
    builder.setAuditorEncryptionKey(auditorEncryptionKeyValue);
    builder.setVoteCount(voteCountValue);
    builder.setResults(resultsValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = digestValue;
        auto const actual = entry.getDigest();
        expectEqualField(expected, actual, "sfDigest");
    }

    {
        auto const& expected = optionCountValue;
        auto const actual = entry.getOptionCount();
        expectEqualField(expected, actual, "sfOptionCount");
    }

    {
        auto const& expected = tallyPublicKeyValue;
        auto const actual = entry.getTallyPublicKey();
        expectEqualField(expected, actual, "sfTallyPublicKey");
    }

    {
        auto const& expected = encryptedTallyValue;
        auto const actual = entry.getEncryptedTally();
        expectEqualField(expected, actual, "sfEncryptedTally");
    }

    {
        auto const& expected = openTimeValue;
        auto const actual = entry.getOpenTime();
        expectEqualField(expected, actual, "sfOpenTime");
    }

    {
        auto const& expected = closeTimeValue;
        auto const actual = entry.getCloseTime();
        expectEqualField(expected, actual, "sfCloseTime");
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
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = entry.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
        EXPECT_TRUE(entry.hasMPTokenIssuanceID());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = entry.getDomainID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(entry.hasDomainID());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = entry.getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry.hasURI());
    }

    {
        auto const& expected = auditorEncryptionKeyValue;
        auto const actualOpt = entry.getAuditorEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptionKey");
        EXPECT_TRUE(entry.hasAuditorEncryptionKey());
    }

    {
        auto const& expected = voteCountValue;
        auto const actualOpt = entry.getVoteCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVoteCount");
        EXPECT_TRUE(entry.hasVoteCount());
    }

    {
        auto const& expected = resultsValue;
        auto const actualOpt = entry.getResults();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfResults");
        EXPECT_TRUE(entry.hasResults());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(BallotTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const domainIDValue = canonical_UINT256();
    auto const digestValue = canonical_UINT256();
    auto const uRIValue = canonical_VL();
    auto const optionCountValue = canonical_UINT8();
    auto const tallyPublicKeyValue = canonical_VL();
    auto const auditorEncryptionKeyValue = canonical_VL();
    auto const encryptedTallyValue = canonical_ARRAY();
    auto const openTimeValue = canonical_UINT32();
    auto const closeTimeValue = canonical_UINT32();
    auto const voteCountValue = canonical_UINT32();
    auto const resultsValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(Ballot::entryType, index);

    sle->at(sfOwner) = ownerValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfMPTokenIssuanceID) = mPTokenIssuanceIDValue;
    sle->at(sfDomainID) = domainIDValue;
    sle->at(sfDigest) = digestValue;
    sle->at(sfURI) = uRIValue;
    sle->at(sfOptionCount) = optionCountValue;
    sle->at(sfTallyPublicKey) = tallyPublicKeyValue;
    sle->at(sfAuditorEncryptionKey) = auditorEncryptionKeyValue;
    sle->setFieldArray(sfEncryptedTally, encryptedTallyValue);
    sle->at(sfOpenTime) = openTimeValue;
    sle->at(sfCloseTime) = closeTimeValue;
    sle->at(sfVoteCount) = voteCountValue;
    sle->setFieldArray(sfResults, resultsValue);
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    BallotBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Ballot entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = digestValue;

        auto const fromSle = entryFromSle.getDigest();
        auto const fromBuilder = entryFromBuilder.getDigest();

        expectEqualField(expected, fromSle, "sfDigest");
        expectEqualField(expected, fromBuilder, "sfDigest");
    }

    {
        auto const& expected = optionCountValue;

        auto const fromSle = entryFromSle.getOptionCount();
        auto const fromBuilder = entryFromBuilder.getOptionCount();

        expectEqualField(expected, fromSle, "sfOptionCount");
        expectEqualField(expected, fromBuilder, "sfOptionCount");
    }

    {
        auto const& expected = tallyPublicKeyValue;

        auto const fromSle = entryFromSle.getTallyPublicKey();
        auto const fromBuilder = entryFromBuilder.getTallyPublicKey();

        expectEqualField(expected, fromSle, "sfTallyPublicKey");
        expectEqualField(expected, fromBuilder, "sfTallyPublicKey");
    }

    {
        auto const& expected = encryptedTallyValue;

        auto const fromSle = entryFromSle.getEncryptedTally();
        auto const fromBuilder = entryFromBuilder.getEncryptedTally();

        expectEqualField(expected, fromSle, "sfEncryptedTally");
        expectEqualField(expected, fromBuilder, "sfEncryptedTally");
    }

    {
        auto const& expected = openTimeValue;

        auto const fromSle = entryFromSle.getOpenTime();
        auto const fromBuilder = entryFromBuilder.getOpenTime();

        expectEqualField(expected, fromSle, "sfOpenTime");
        expectEqualField(expected, fromBuilder, "sfOpenTime");
    }

    {
        auto const& expected = closeTimeValue;

        auto const fromSle = entryFromSle.getCloseTime();
        auto const fromBuilder = entryFromBuilder.getCloseTime();

        expectEqualField(expected, fromSle, "sfCloseTime");
        expectEqualField(expected, fromBuilder, "sfCloseTime");
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
        auto const& expected = mPTokenIssuanceIDValue;

        auto const fromSleOpt = entryFromSle.getMPTokenIssuanceID();
        auto const fromBuilderOpt = entryFromBuilder.getMPTokenIssuanceID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMPTokenIssuanceID");
        expectEqualField(expected, *fromBuilderOpt, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = domainIDValue;

        auto const fromSleOpt = entryFromSle.getDomainID();
        auto const fromBuilderOpt = entryFromBuilder.getDomainID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDomainID");
        expectEqualField(expected, *fromBuilderOpt, "sfDomainID");
    }

    {
        auto const& expected = uRIValue;

        auto const fromSleOpt = entryFromSle.getURI();
        auto const fromBuilderOpt = entryFromBuilder.getURI();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfURI");
        expectEqualField(expected, *fromBuilderOpt, "sfURI");
    }

    {
        auto const& expected = auditorEncryptionKeyValue;

        auto const fromSleOpt = entryFromSle.getAuditorEncryptionKey();
        auto const fromBuilderOpt = entryFromBuilder.getAuditorEncryptionKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuditorEncryptionKey");
        expectEqualField(expected, *fromBuilderOpt, "sfAuditorEncryptionKey");
    }

    {
        auto const& expected = voteCountValue;

        auto const fromSleOpt = entryFromSle.getVoteCount();
        auto const fromBuilderOpt = entryFromBuilder.getVoteCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVoteCount");
        expectEqualField(expected, *fromBuilderOpt, "sfVoteCount");
    }

    {
        auto const& expected = resultsValue;

        auto const fromSleOpt = entryFromSle.getResults();
        auto const fromBuilderOpt = entryFromBuilder.getResults();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfResults");
        expectEqualField(expected, *fromBuilderOpt, "sfResults");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(BallotTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Ballot{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(BallotTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(BallotBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(BallotTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const digestValue = canonical_UINT256();
    auto const optionCountValue = canonical_UINT8();
    auto const tallyPublicKeyValue = canonical_VL();
    auto const encryptedTallyValue = canonical_ARRAY();
    auto const openTimeValue = canonical_UINT32();
    auto const closeTimeValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    BallotBuilder builder{
        ownerValue,
        sequenceValue,
        digestValue,
        optionCountValue,
        tallyPublicKeyValue,
        encryptedTallyValue,
        openTimeValue,
        closeTimeValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasMPTokenIssuanceID());
    EXPECT_FALSE(entry.getMPTokenIssuanceID().has_value());
    EXPECT_FALSE(entry.hasDomainID());
    EXPECT_FALSE(entry.getDomainID().has_value());
    EXPECT_FALSE(entry.hasURI());
    EXPECT_FALSE(entry.getURI().has_value());
    EXPECT_FALSE(entry.hasAuditorEncryptionKey());
    EXPECT_FALSE(entry.getAuditorEncryptionKey().has_value());
    EXPECT_FALSE(entry.hasVoteCount());
    EXPECT_FALSE(entry.getVoteCount().has_value());
    EXPECT_FALSE(entry.hasResults());
    EXPECT_FALSE(entry.getResults().has_value());
}
}
