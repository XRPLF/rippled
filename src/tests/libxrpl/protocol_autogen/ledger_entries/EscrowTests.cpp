// Auto-generated unit tests for ledger entry Escrow


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Escrow.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(EscrowTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const conditionValue = canonical_VL();
    auto const cancelAfterValue = canonical_UINT32();
    auto const finishAfterValue = canonical_UINT32();
    auto const sourceTagValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();
    auto const transferRateValue = canonical_UINT32();
    auto const issuerNodeValue = canonical_UINT64();

    EscrowBuilder builder{
    };

    builder.setAccount(accountValue);
    builder.setSequence(sequenceValue);
    builder.setDestination(destinationValue);
    builder.setAmount(amountValue);
    builder.setCondition(conditionValue);
    builder.setCancelAfter(cancelAfterValue);
    builder.setFinishAfter(finishAfterValue);
    builder.setSourceTag(sourceTagValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);
    builder.setDestinationNode(destinationNodeValue);
    builder.setTransferRate(transferRateValue);
    builder.setIssuerNode(issuerNodeValue);

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
        auto const& expected = sequenceValue;
        auto const actualOpt = entry.getSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSequence");
        EXPECT_TRUE(entry.hasSequence());
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = entry.getDestination();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(entry.hasDestination());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = entry.getAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(entry.hasAmount());
    }

    {
        auto const& expected = conditionValue;
        auto const actualOpt = entry.getCondition();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCondition");
        EXPECT_TRUE(entry.hasCondition());
    }

    {
        auto const& expected = cancelAfterValue;
        auto const actualOpt = entry.getCancelAfter();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCancelAfter");
        EXPECT_TRUE(entry.hasCancelAfter());
    }

    {
        auto const& expected = finishAfterValue;
        auto const actualOpt = entry.getFinishAfter();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFinishAfter");
        EXPECT_TRUE(entry.hasFinishAfter());
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
        auto const& expected = ownerNodeValue;
        auto const actualOpt = entry.getOwnerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerNode");
        EXPECT_TRUE(entry.hasOwnerNode());
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

    {
        auto const& expected = destinationNodeValue;
        auto const actualOpt = entry.getDestinationNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestinationNode");
        EXPECT_TRUE(entry.hasDestinationNode());
    }

    {
        auto const& expected = transferRateValue;
        auto const actualOpt = entry.getTransferRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTransferRate");
        EXPECT_TRUE(entry.hasTransferRate());
    }

    {
        auto const& expected = issuerNodeValue;
        auto const actualOpt = entry.getIssuerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuerNode");
        EXPECT_TRUE(entry.hasIssuerNode());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(EscrowTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const conditionValue = canonical_VL();
    auto const cancelAfterValue = canonical_UINT32();
    auto const finishAfterValue = canonical_UINT32();
    auto const sourceTagValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();
    auto const transferRateValue = canonical_UINT32();
    auto const issuerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(Escrow::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfDestination) = destinationValue;
    sle->at(sfAmount) = amountValue;
    sle->at(sfCondition) = conditionValue;
    sle->at(sfCancelAfter) = cancelAfterValue;
    sle->at(sfFinishAfter) = finishAfterValue;
    sle->at(sfSourceTag) = sourceTagValue;
    sle->at(sfDestinationTag) = destinationTagValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfDestinationNode) = destinationNodeValue;
    sle->at(sfTransferRate) = transferRateValue;
    sle->at(sfIssuerNode) = issuerNodeValue;

    EscrowBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Escrow entryFromSle{sle};
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
        auto const& expected = sequenceValue;

        auto const fromSleOpt = entryFromSle.getSequence();
        auto const fromBuilderOpt = entryFromBuilder.getSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfSequence");
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
        auto const& expected = amountValue;

        auto const fromSleOpt = entryFromSle.getAmount();
        auto const fromBuilderOpt = entryFromBuilder.getAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfAmount");
    }

    {
        auto const& expected = conditionValue;

        auto const fromSleOpt = entryFromSle.getCondition();
        auto const fromBuilderOpt = entryFromBuilder.getCondition();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCondition");
        expectEqualField(expected, *fromBuilderOpt, "sfCondition");
    }

    {
        auto const& expected = cancelAfterValue;

        auto const fromSleOpt = entryFromSle.getCancelAfter();
        auto const fromBuilderOpt = entryFromBuilder.getCancelAfter();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCancelAfter");
        expectEqualField(expected, *fromBuilderOpt, "sfCancelAfter");
    }

    {
        auto const& expected = finishAfterValue;

        auto const fromSleOpt = entryFromSle.getFinishAfter();
        auto const fromBuilderOpt = entryFromBuilder.getFinishAfter();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFinishAfter");
        expectEqualField(expected, *fromBuilderOpt, "sfFinishAfter");
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
        auto const& expected = ownerNodeValue;

        auto const fromSleOpt = entryFromSle.getOwnerNode();
        auto const fromBuilderOpt = entryFromBuilder.getOwnerNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwnerNode");
        expectEqualField(expected, *fromBuilderOpt, "sfOwnerNode");
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
        auto const& expected = transferRateValue;

        auto const fromSleOpt = entryFromSle.getTransferRate();
        auto const fromBuilderOpt = entryFromBuilder.getTransferRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTransferRate");
        expectEqualField(expected, *fromBuilderOpt, "sfTransferRate");
    }

    {
        auto const& expected = issuerNodeValue;

        auto const fromSleOpt = entryFromSle.getIssuerNode();
        auto const fromBuilderOpt = entryFromBuilder.getIssuerNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuerNode");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuerNode");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(EscrowTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Escrow{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(EscrowTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(EscrowBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(EscrowTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    EscrowBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAccount());
    EXPECT_FALSE(entry.getAccount().has_value());
    EXPECT_FALSE(entry.hasSequence());
    EXPECT_FALSE(entry.getSequence().has_value());
    EXPECT_FALSE(entry.hasDestination());
    EXPECT_FALSE(entry.getDestination().has_value());
    EXPECT_FALSE(entry.hasAmount());
    EXPECT_FALSE(entry.getAmount().has_value());
    EXPECT_FALSE(entry.hasCondition());
    EXPECT_FALSE(entry.getCondition().has_value());
    EXPECT_FALSE(entry.hasCancelAfter());
    EXPECT_FALSE(entry.getCancelAfter().has_value());
    EXPECT_FALSE(entry.hasFinishAfter());
    EXPECT_FALSE(entry.getFinishAfter().has_value());
    EXPECT_FALSE(entry.hasSourceTag());
    EXPECT_FALSE(entry.getSourceTag().has_value());
    EXPECT_FALSE(entry.hasDestinationTag());
    EXPECT_FALSE(entry.getDestinationTag().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
    EXPECT_FALSE(entry.hasDestinationNode());
    EXPECT_FALSE(entry.getDestinationNode().has_value());
    EXPECT_FALSE(entry.hasTransferRate());
    EXPECT_FALSE(entry.getTransferRate().has_value());
    EXPECT_FALSE(entry.hasIssuerNode());
    EXPECT_FALSE(entry.getIssuerNode().has_value());
}
}
