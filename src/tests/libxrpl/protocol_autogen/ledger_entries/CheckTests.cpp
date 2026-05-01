// Auto-generated unit tests for ledger entry Check


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Check.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(CheckTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const destinationNodeValue = canonical_UINT64();
    auto const expirationValue = canonical_UINT32();
    auto const invoiceIDValue = canonical_UINT256();
    auto const sourceTagValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    CheckBuilder builder{
    };

    builder.setAccount(accountValue);
    builder.setDestination(destinationValue);
    builder.setSendMax(sendMaxValue);
    builder.setSequence(sequenceValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setDestinationNode(destinationNodeValue);
    builder.setExpiration(expirationValue);
    builder.setInvoiceID(invoiceIDValue);
    builder.setSourceTag(sourceTagValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = accountValue;
        auto const actualOpt = entry.getAccount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAccount");
        EXPECT_TRUE(entry.hasAccount());
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = entry.getDestination();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(entry.hasDestination());
    }

    {
        auto const& expected = sendMaxValue;
        auto const actualOpt = entry.getSendMax();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSendMax");
        EXPECT_TRUE(entry.hasSendMax());
    }

    {
        auto const& expected = sequenceValue;
        auto const actualOpt = entry.getSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSequence");
        EXPECT_TRUE(entry.hasSequence());
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actualOpt = entry.getOwnerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerNode");
        EXPECT_TRUE(entry.hasOwnerNode());
    }

    {
        auto const& expected = destinationNodeValue;
        auto const actualOpt = entry.getDestinationNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestinationNode");
        EXPECT_TRUE(entry.hasDestinationNode());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = entry.getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
        EXPECT_TRUE(entry.hasInvoiceID());
    }

    {
        auto const& expected = sourceTagValue;
        auto const actualOpt = entry.getSourceTag();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSourceTag");
        EXPECT_TRUE(entry.hasSourceTag());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = entry.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(entry.hasDestinationTag());
    }

    {
        auto const& expected = previousTxnIDValue;
        auto const actualOpt = entry.getPreviousTxnID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousTxnID");
        EXPECT_TRUE(entry.hasPreviousTxnID());
    }

    {
        auto const& expected = previousTxnLgrSeqValue;
        auto const actualOpt = entry.getPreviousTxnLgrSeq();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousTxnLgrSeq");
        EXPECT_TRUE(entry.hasPreviousTxnLgrSeq());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(CheckTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const destinationNodeValue = canonical_UINT64();
    auto const expirationValue = canonical_UINT32();
    auto const invoiceIDValue = canonical_UINT256();
    auto const sourceTagValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(Check::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfDestination) = destinationValue;
    sle->at(sfSendMax) = sendMaxValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfDestinationNode) = destinationNodeValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfInvoiceID) = invoiceIDValue;
    sle->at(sfSourceTag) = sourceTagValue;
    sle->at(sfDestinationTag) = destinationTagValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    CheckBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Check entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSleOpt = entryFromSle.getAccount();
        auto const fromBuilderOpt = entryFromBuilder.getAccount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAccount");
        expectEqualField(expected, *fromBuilderOpt, "sfAccount");
    }

    {
        auto const& expected = destinationValue;

        auto const fromSleOpt = entryFromSle.getDestination();
        auto const fromBuilderOpt = entryFromBuilder.getDestination();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDestination");
        expectEqualField(expected, *fromBuilderOpt, "sfDestination");
    }

    {
        auto const& expected = sendMaxValue;

        auto const fromSleOpt = entryFromSle.getSendMax();
        auto const fromBuilderOpt = entryFromBuilder.getSendMax();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSendMax");
        expectEqualField(expected, *fromBuilderOpt, "sfSendMax");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSleOpt = entryFromSle.getSequence();
        auto const fromBuilderOpt = entryFromBuilder.getSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSleOpt = entryFromSle.getOwnerNode();
        auto const fromBuilderOpt = entryFromBuilder.getOwnerNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwnerNode");
        expectEqualField(expected, *fromBuilderOpt, "sfOwnerNode");
    }

    {
        auto const& expected = destinationNodeValue;

        auto const fromSleOpt = entryFromSle.getDestinationNode();
        auto const fromBuilderOpt = entryFromBuilder.getDestinationNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDestinationNode");
        expectEqualField(expected, *fromBuilderOpt, "sfDestinationNode");
    }

    {
        auto const& expected = expirationValue;

        auto const fromSleOpt = entryFromSle.getExpiration();
        auto const fromBuilderOpt = entryFromBuilder.getExpiration();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExpiration");
        expectEqualField(expected, *fromBuilderOpt, "sfExpiration");
    }

    {
        auto const& expected = invoiceIDValue;

        auto const fromSleOpt = entryFromSle.getInvoiceID();
        auto const fromBuilderOpt = entryFromBuilder.getInvoiceID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfInvoiceID");
        expectEqualField(expected, *fromBuilderOpt, "sfInvoiceID");
    }

    {
        auto const& expected = sourceTagValue;

        auto const fromSleOpt = entryFromSle.getSourceTag();
        auto const fromBuilderOpt = entryFromBuilder.getSourceTag();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSourceTag");
        expectEqualField(expected, *fromBuilderOpt, "sfSourceTag");
    }

    {
        auto const& expected = destinationTagValue;

        auto const fromSleOpt = entryFromSle.getDestinationTag();
        auto const fromBuilderOpt = entryFromBuilder.getDestinationTag();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDestinationTag");
        expectEqualField(expected, *fromBuilderOpt, "sfDestinationTag");
    }

    {
        auto const& expected = previousTxnIDValue;

        auto const fromSleOpt = entryFromSle.getPreviousTxnID();
        auto const fromBuilderOpt = entryFromBuilder.getPreviousTxnID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousTxnID");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;

        auto const fromSleOpt = entryFromSle.getPreviousTxnLgrSeq();
        auto const fromBuilderOpt = entryFromBuilder.getPreviousTxnLgrSeq();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousTxnLgrSeq");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousTxnLgrSeq");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(CheckTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Check{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(CheckTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(CheckBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(CheckTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    CheckBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAccount());
    EXPECT_FALSE(entry.getAccount().has_value());
    EXPECT_FALSE(entry.hasDestination());
    EXPECT_FALSE(entry.getDestination().has_value());
    EXPECT_FALSE(entry.hasSendMax());
    EXPECT_FALSE(entry.getSendMax().has_value());
    EXPECT_FALSE(entry.hasSequence());
    EXPECT_FALSE(entry.getSequence().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasDestinationNode());
    EXPECT_FALSE(entry.getDestinationNode().has_value());
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
    EXPECT_FALSE(entry.hasInvoiceID());
    EXPECT_FALSE(entry.getInvoiceID().has_value());
    EXPECT_FALSE(entry.hasSourceTag());
    EXPECT_FALSE(entry.getSourceTag().has_value());
    EXPECT_FALSE(entry.hasDestinationTag());
    EXPECT_FALSE(entry.getDestinationTag().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
