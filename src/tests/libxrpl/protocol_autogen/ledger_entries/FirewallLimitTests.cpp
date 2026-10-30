// Auto-generated unit tests for ledger entry FirewallLimit


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/FirewallLimit.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(FirewallLimitTests, BuilderSettersRoundTrip)
{
    UInt256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const limitSignerValue = canonical_ACCOUNT();
    auto const limitAmountValue = canonical_AMOUNT();
    auto const periodValue = canonical_UINT32();
    auto const spentValue = canonical_AMOUNT();
    auto const startDateValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    FirewallLimitBuilder builder{
        ownerValue,
        assetValue,
        limitAmountValue,
        periodValue,
        spentValue,
        startDateValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setLimitSigner(limitSignerValue);

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
        auto const& expected = assetValue;
        auto const actual = entry.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = limitAmountValue;
        auto const actual = entry.getLimitAmount();
        expectEqualField(expected, actual, "sfLimitAmount");
    }

    {
        auto const& expected = periodValue;
        auto const actual = entry.getPeriod();
        expectEqualField(expected, actual, "sfPeriod");
    }

    {
        auto const& expected = spentValue;
        auto const actual = entry.getSpent();
        expectEqualField(expected, actual, "sfSpent");
    }

    {
        auto const& expected = startDateValue;
        auto const actual = entry.getStartDate();
        expectEqualField(expected, actual, "sfStartDate");
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
        auto const& expected = limitSignerValue;
        auto const actualOpt = entry.getLimitSigner();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLimitSigner");
        EXPECT_TRUE(entry.hasLimitSigner());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(FirewallLimitTests, BuilderFromSleRoundTrip)
{
    UInt256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const limitSignerValue = canonical_ACCOUNT();
    auto const limitAmountValue = canonical_AMOUNT();
    auto const periodValue = canonical_UINT32();
    auto const spentValue = canonical_AMOUNT();
    auto const startDateValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(FirewallLimit::entryType, index);

    sle->at(sfOwner) = ownerValue;
    sle->at(sfAsset) = STIssue(sfAsset, assetValue);
    sle->at(sfLimitSigner) = limitSignerValue;
    sle->at(sfLimitAmount) = limitAmountValue;
    sle->at(sfPeriod) = periodValue;
    sle->at(sfSpent) = spentValue;
    sle->at(sfStartDate) = startDateValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    FirewallLimitBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    FirewallLimit entryFromSle{sle};
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
        auto const& expected = assetValue;

        auto const fromSle = entryFromSle.getAsset();
        auto const fromBuilder = entryFromBuilder.getAsset();

        expectEqualField(expected, fromSle, "sfAsset");
        expectEqualField(expected, fromBuilder, "sfAsset");
    }

    {
        auto const& expected = limitAmountValue;

        auto const fromSle = entryFromSle.getLimitAmount();
        auto const fromBuilder = entryFromBuilder.getLimitAmount();

        expectEqualField(expected, fromSle, "sfLimitAmount");
        expectEqualField(expected, fromBuilder, "sfLimitAmount");
    }

    {
        auto const& expected = periodValue;

        auto const fromSle = entryFromSle.getPeriod();
        auto const fromBuilder = entryFromBuilder.getPeriod();

        expectEqualField(expected, fromSle, "sfPeriod");
        expectEqualField(expected, fromBuilder, "sfPeriod");
    }

    {
        auto const& expected = spentValue;

        auto const fromSle = entryFromSle.getSpent();
        auto const fromBuilder = entryFromBuilder.getSpent();

        expectEqualField(expected, fromSle, "sfSpent");
        expectEqualField(expected, fromBuilder, "sfSpent");
    }

    {
        auto const& expected = startDateValue;

        auto const fromSle = entryFromSle.getStartDate();
        auto const fromBuilder = entryFromBuilder.getStartDate();

        expectEqualField(expected, fromSle, "sfStartDate");
        expectEqualField(expected, fromBuilder, "sfStartDate");
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
        auto const& expected = limitSignerValue;

        auto const fromSleOpt = entryFromSle.getLimitSigner();
        auto const fromBuilderOpt = entryFromBuilder.getLimitSigner();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLimitSigner");
        expectEqualField(expected, *fromBuilderOpt, "sfLimitSigner");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(FirewallLimitTests, WrapperThrowsOnWrongEntryType)
{
    UInt256 const index{3u};

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

    EXPECT_THROW(FirewallLimit{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(FirewallLimitTests, BuilderThrowsOnWrongEntryType)
{
    UInt256 const index{4u};

    // Build a valid ledger entry of a different type
    TicketBuilder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(FirewallLimitBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(FirewallLimitTests, OptionalFieldsReturnNullopt)
{
    UInt256 const index{3u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const limitAmountValue = canonical_AMOUNT();
    auto const periodValue = canonical_UINT32();
    auto const spentValue = canonical_AMOUNT();
    auto const startDateValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    FirewallLimitBuilder builder{
        ownerValue,
        assetValue,
        limitAmountValue,
        periodValue,
        spentValue,
        startDateValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasLimitSigner());
    EXPECT_FALSE(entry.getLimitSigner().has_value());
}
}
