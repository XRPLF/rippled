// Auto-generated unit tests for ledger entry Subscription


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Subscription.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(SubscriptionTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const frequencyValue = canonical_UINT32();
    auto const nextClaimTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();

    SubscriptionBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        accountValue,
        destinationValue,
        amountValue,
        balanceValue,
        frequencyValue,
        nextClaimTimeValue,
        destinationNodeValue
    };

    builder.setDestinationTag(destinationTagValue);
    builder.setExpiration(expirationValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

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
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = destinationValue;
        auto const actual = entry.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = entry.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = balanceValue;
        auto const actual = entry.getBalance();
        expectEqualField(expected, actual, "sfBalance");
    }

    {
        auto const& expected = frequencyValue;
        auto const actual = entry.getFrequency();
        expectEqualField(expected, actual, "sfFrequency");
    }

    {
        auto const& expected = nextClaimTimeValue;
        auto const actual = entry.getNextClaimTime();
        expectEqualField(expected, actual, "sfNextClaimTime");
    }

    {
        auto const& expected = destinationNodeValue;
        auto const actual = entry.getDestinationNode();
        expectEqualField(expected, actual, "sfDestinationNode");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = entry.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(entry.hasDestinationTag());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(SubscriptionTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const frequencyValue = canonical_UINT32();
    auto const nextClaimTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(Subscription::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfDestination) = destinationValue;
    sle->at(sfDestinationTag) = destinationTagValue;
    sle->at(sfAmount) = amountValue;
    sle->at(sfBalance) = balanceValue;
    sle->at(sfFrequency) = frequencyValue;
    sle->at(sfNextClaimTime) = nextClaimTimeValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfDestinationNode) = destinationNodeValue;

    SubscriptionBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Subscription entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

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
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = destinationValue;

        auto const fromSle = entryFromSle.getDestination();
        auto const fromBuilder = entryFromBuilder.getDestination();

        expectEqualField(expected, fromSle, "sfDestination");
        expectEqualField(expected, fromBuilder, "sfDestination");
    }

    {
        auto const& expected = amountValue;

        auto const fromSle = entryFromSle.getAmount();
        auto const fromBuilder = entryFromBuilder.getAmount();

        expectEqualField(expected, fromSle, "sfAmount");
        expectEqualField(expected, fromBuilder, "sfAmount");
    }

    {
        auto const& expected = balanceValue;

        auto const fromSle = entryFromSle.getBalance();
        auto const fromBuilder = entryFromBuilder.getBalance();

        expectEqualField(expected, fromSle, "sfBalance");
        expectEqualField(expected, fromBuilder, "sfBalance");
    }

    {
        auto const& expected = frequencyValue;

        auto const fromSle = entryFromSle.getFrequency();
        auto const fromBuilder = entryFromBuilder.getFrequency();

        expectEqualField(expected, fromSle, "sfFrequency");
        expectEqualField(expected, fromBuilder, "sfFrequency");
    }

    {
        auto const& expected = nextClaimTimeValue;

        auto const fromSle = entryFromSle.getNextClaimTime();
        auto const fromBuilder = entryFromBuilder.getNextClaimTime();

        expectEqualField(expected, fromSle, "sfNextClaimTime");
        expectEqualField(expected, fromBuilder, "sfNextClaimTime");
    }

    {
        auto const& expected = destinationNodeValue;

        auto const fromSle = entryFromSle.getDestinationNode();
        auto const fromBuilder = entryFromBuilder.getDestinationNode();

        expectEqualField(expected, fromSle, "sfDestinationNode");
        expectEqualField(expected, fromBuilder, "sfDestinationNode");
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
        auto const& expected = expirationValue;

        auto const fromSleOpt = entryFromSle.getExpiration();
        auto const fromBuilderOpt = entryFromBuilder.getExpiration();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExpiration");
        expectEqualField(expected, *fromBuilderOpt, "sfExpiration");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(SubscriptionTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Subscription{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(SubscriptionTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(SubscriptionBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(SubscriptionTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const accountValue = canonical_ACCOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const frequencyValue = canonical_UINT32();
    auto const nextClaimTimeValue = canonical_UINT32();
    auto const destinationNodeValue = canonical_UINT64();

    SubscriptionBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        accountValue,
        destinationValue,
        amountValue,
        balanceValue,
        frequencyValue,
        nextClaimTimeValue,
        destinationNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasDestinationTag());
    EXPECT_FALSE(entry.getDestinationTag().has_value());
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
}
}
