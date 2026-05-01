// Auto-generated unit tests for ledger entry PayChannel


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/PayChannel.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(PayChannelTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const publicKeyValue = canonical_VL();
    auto const settleDelayValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const cancelAfterValue = canonical_UINT32();
    auto const sourceTagValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();

    PayChannelBuilder builder{
    };

    builder.setAccount(accountValue);
    builder.setDestination(destinationValue);
    builder.setSequence(sequenceValue);
    builder.setAmount(amountValue);
    builder.setBalance(balanceValue);
    builder.setPublicKey(publicKeyValue);
    builder.setSettleDelay(settleDelayValue);
    builder.setExpiration(expirationValue);
    builder.setCancelAfter(cancelAfterValue);
    builder.setSourceTag(sourceTagValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);
    builder.setDestinationNode(destinationNodeValue);

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
        auto const& expected = sequenceValue;
        auto const actualOpt = entry.getSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSequence");
        EXPECT_TRUE(entry.hasSequence());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = entry.getAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(entry.hasAmount());
    }

    {
        auto const& expected = balanceValue;
        auto const actualOpt = entry.getBalance();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBalance");
        EXPECT_TRUE(entry.hasBalance());
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = entry.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPublicKey");
        EXPECT_TRUE(entry.hasPublicKey());
    }

    {
        auto const& expected = settleDelayValue;
        auto const actualOpt = entry.getSettleDelay();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSettleDelay");
        EXPECT_TRUE(entry.hasSettleDelay());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
    }

    {
        auto const& expected = cancelAfterValue;
        auto const actualOpt = entry.getCancelAfter();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCancelAfter");
        EXPECT_TRUE(entry.hasCancelAfter());
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

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(PayChannelTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const publicKeyValue = canonical_VL();
    auto const settleDelayValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const cancelAfterValue = canonical_UINT32();
    auto const sourceTagValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(PayChannel::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfDestination) = destinationValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfAmount) = amountValue;
    sle->at(sfBalance) = balanceValue;
    sle->at(sfPublicKey) = publicKeyValue;
    sle->at(sfSettleDelay) = settleDelayValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfCancelAfter) = cancelAfterValue;
    sle->at(sfSourceTag) = sourceTagValue;
    sle->at(sfDestinationTag) = destinationTagValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfDestinationNode) = destinationNodeValue;

    PayChannelBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    PayChannel entryFromSle{sle};
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
        auto const& expected = sequenceValue;

        auto const fromSleOpt = entryFromSle.getSequence();
        auto const fromBuilderOpt = entryFromBuilder.getSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfSequence");
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
        auto const& expected = balanceValue;

        auto const fromSleOpt = entryFromSle.getBalance();
        auto const fromBuilderOpt = entryFromBuilder.getBalance();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBalance");
        expectEqualField(expected, *fromBuilderOpt, "sfBalance");
    }

    {
        auto const& expected = publicKeyValue;

        auto const fromSleOpt = entryFromSle.getPublicKey();
        auto const fromBuilderOpt = entryFromBuilder.getPublicKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPublicKey");
        expectEqualField(expected, *fromBuilderOpt, "sfPublicKey");
    }

    {
        auto const& expected = settleDelayValue;

        auto const fromSleOpt = entryFromSle.getSettleDelay();
        auto const fromBuilderOpt = entryFromBuilder.getSettleDelay();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSettleDelay");
        expectEqualField(expected, *fromBuilderOpt, "sfSettleDelay");
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
        auto const& expected = cancelAfterValue;

        auto const fromSleOpt = entryFromSle.getCancelAfter();
        auto const fromBuilderOpt = entryFromBuilder.getCancelAfter();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCancelAfter");
        expectEqualField(expected, *fromBuilderOpt, "sfCancelAfter");
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

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(PayChannelTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(PayChannel{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(PayChannelTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(PayChannelBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(PayChannelTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    PayChannelBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAccount());
    EXPECT_FALSE(entry.getAccount().has_value());
    EXPECT_FALSE(entry.hasDestination());
    EXPECT_FALSE(entry.getDestination().has_value());
    EXPECT_FALSE(entry.hasSequence());
    EXPECT_FALSE(entry.getSequence().has_value());
    EXPECT_FALSE(entry.hasAmount());
    EXPECT_FALSE(entry.getAmount().has_value());
    EXPECT_FALSE(entry.hasBalance());
    EXPECT_FALSE(entry.getBalance().has_value());
    EXPECT_FALSE(entry.hasPublicKey());
    EXPECT_FALSE(entry.getPublicKey().has_value());
    EXPECT_FALSE(entry.hasSettleDelay());
    EXPECT_FALSE(entry.getSettleDelay().has_value());
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
    EXPECT_FALSE(entry.hasCancelAfter());
    EXPECT_FALSE(entry.getCancelAfter().has_value());
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
}
}
