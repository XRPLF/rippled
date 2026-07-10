// Auto-generated unit tests for ledger entry Sponsorship


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Sponsorship.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(SponsorshipTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerValue = canonical_ACCOUNT();
    auto const sponseeValue = canonical_ACCOUNT();
    auto const feeAmountValue = canonical_AMOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const remainingOwnerCountValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const sponseeNodeValue = canonical_UINT64();

    SponsorshipBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerValue,
        sponseeValue,
        ownerNodeValue,
        sponseeNodeValue
    };

    builder.setFeeAmount(feeAmountValue);
    builder.setMaxFee(maxFeeValue);
    builder.setRemainingOwnerCount(remainingOwnerCountValue);

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
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = sponseeValue;
        auto const actual = entry.getSponsee();
        expectEqualField(expected, actual, "sfSponsee");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = sponseeNodeValue;
        auto const actual = entry.getSponseeNode();
        expectEqualField(expected, actual, "sfSponseeNode");
    }

    {
        auto const& expected = feeAmountValue;
        auto const actualOpt = entry.getFeeAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFeeAmount");
        EXPECT_TRUE(entry.hasFeeAmount());
    }

    {
        auto const& expected = maxFeeValue;
        auto const actualOpt = entry.getMaxFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMaxFee");
        EXPECT_TRUE(entry.hasMaxFee());
    }

    {
        auto const& expected = remainingOwnerCountValue;
        auto const actualOpt = entry.getRemainingOwnerCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfRemainingOwnerCount");
        EXPECT_TRUE(entry.hasRemainingOwnerCount());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(SponsorshipTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerValue = canonical_ACCOUNT();
    auto const sponseeValue = canonical_ACCOUNT();
    auto const feeAmountValue = canonical_AMOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const remainingOwnerCountValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const sponseeNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(Sponsorship::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfOwner) = ownerValue;
    sle->at(sfSponsee) = sponseeValue;
    sle->at(sfFeeAmount) = feeAmountValue;
    sle->at(sfMaxFee) = maxFeeValue;
    sle->at(sfRemainingOwnerCount) = remainingOwnerCountValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfSponseeNode) = sponseeNodeValue;

    SponsorshipBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Sponsorship entryFromSle{sle};
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
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = sponseeValue;

        auto const fromSle = entryFromSle.getSponsee();
        auto const fromBuilder = entryFromBuilder.getSponsee();

        expectEqualField(expected, fromSle, "sfSponsee");
        expectEqualField(expected, fromBuilder, "sfSponsee");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = sponseeNodeValue;

        auto const fromSle = entryFromSle.getSponseeNode();
        auto const fromBuilder = entryFromBuilder.getSponseeNode();

        expectEqualField(expected, fromSle, "sfSponseeNode");
        expectEqualField(expected, fromBuilder, "sfSponseeNode");
    }

    {
        auto const& expected = feeAmountValue;

        auto const fromSleOpt = entryFromSle.getFeeAmount();
        auto const fromBuilderOpt = entryFromBuilder.getFeeAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFeeAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfFeeAmount");
    }

    {
        auto const& expected = maxFeeValue;

        auto const fromSleOpt = entryFromSle.getMaxFee();
        auto const fromBuilderOpt = entryFromBuilder.getMaxFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMaxFee");
        expectEqualField(expected, *fromBuilderOpt, "sfMaxFee");
    }

    {
        auto const& expected = remainingOwnerCountValue;

        auto const fromSleOpt = entryFromSle.getRemainingOwnerCount();
        auto const fromBuilderOpt = entryFromBuilder.getRemainingOwnerCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfRemainingOwnerCount");
        expectEqualField(expected, *fromBuilderOpt, "sfRemainingOwnerCount");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(SponsorshipTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Sponsorship{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(SponsorshipTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(SponsorshipBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(SponsorshipTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerValue = canonical_ACCOUNT();
    auto const sponseeValue = canonical_ACCOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const sponseeNodeValue = canonical_UINT64();

    SponsorshipBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerValue,
        sponseeValue,
        ownerNodeValue,
        sponseeNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasFeeAmount());
    EXPECT_FALSE(entry.getFeeAmount().has_value());
    EXPECT_FALSE(entry.hasMaxFee());
    EXPECT_FALSE(entry.getMaxFee().has_value());
    EXPECT_FALSE(entry.hasRemainingOwnerCount());
    EXPECT_FALSE(entry.getRemainingOwnerCount().has_value());
}
}
