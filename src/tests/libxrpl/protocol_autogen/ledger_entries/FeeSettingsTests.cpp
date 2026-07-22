// Auto-generated unit tests for ledger entry FeeSettings


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/FeeSettings.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(FeeSettingsTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const baseFeeValue = canonical_UINT64();
    auto const referenceFeeUnitsValue = canonical_UINT32();
    auto const reserveBaseValue = canonical_UINT32();
    auto const reserveIncrementValue = canonical_UINT32();
    auto const baseFeeDropsValue = canonical_AMOUNT();
    auto const reserveBaseDropsValue = canonical_AMOUNT();
    auto const reserveIncrementDropsValue = canonical_AMOUNT();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    FeeSettingsBuilder builder{
    };

    builder.setBaseFee(baseFeeValue);
    builder.setReferenceFeeUnits(referenceFeeUnitsValue);
    builder.setReserveBase(reserveBaseValue);
    builder.setReserveIncrement(reserveIncrementValue);
    builder.setBaseFeeDrops(baseFeeDropsValue);
    builder.setReserveBaseDrops(reserveBaseDropsValue);
    builder.setReserveIncrementDrops(reserveIncrementDropsValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = baseFeeValue;
        auto const actualOpt = entry.getBaseFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBaseFee");
        EXPECT_TRUE(entry.hasBaseFee());
    }

    {
        auto const& expected = referenceFeeUnitsValue;
        auto const actualOpt = entry.getReferenceFeeUnits();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfReferenceFeeUnits");
        EXPECT_TRUE(entry.hasReferenceFeeUnits());
    }

    {
        auto const& expected = reserveBaseValue;
        auto const actualOpt = entry.getReserveBase();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfReserveBase");
        EXPECT_TRUE(entry.hasReserveBase());
    }

    {
        auto const& expected = reserveIncrementValue;
        auto const actualOpt = entry.getReserveIncrement();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfReserveIncrement");
        EXPECT_TRUE(entry.hasReserveIncrement());
    }

    {
        auto const& expected = baseFeeDropsValue;
        auto const actualOpt = entry.getBaseFeeDrops();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBaseFeeDrops");
        EXPECT_TRUE(entry.hasBaseFeeDrops());
    }

    {
        auto const& expected = reserveBaseDropsValue;
        auto const actualOpt = entry.getReserveBaseDrops();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfReserveBaseDrops");
        EXPECT_TRUE(entry.hasReserveBaseDrops());
    }

    {
        auto const& expected = reserveIncrementDropsValue;
        auto const actualOpt = entry.getReserveIncrementDrops();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfReserveIncrementDrops");
        EXPECT_TRUE(entry.hasReserveIncrementDrops());
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
TEST(FeeSettingsTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const baseFeeValue = canonical_UINT64();
    auto const referenceFeeUnitsValue = canonical_UINT32();
    auto const reserveBaseValue = canonical_UINT32();
    auto const reserveIncrementValue = canonical_UINT32();
    auto const baseFeeDropsValue = canonical_AMOUNT();
    auto const reserveBaseDropsValue = canonical_AMOUNT();
    auto const reserveIncrementDropsValue = canonical_AMOUNT();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(FeeSettings::entryType, index);

    sle->at(sfBaseFee) = baseFeeValue;
    sle->at(sfReferenceFeeUnits) = referenceFeeUnitsValue;
    sle->at(sfReserveBase) = reserveBaseValue;
    sle->at(sfReserveIncrement) = reserveIncrementValue;
    sle->at(sfBaseFeeDrops) = baseFeeDropsValue;
    sle->at(sfReserveBaseDrops) = reserveBaseDropsValue;
    sle->at(sfReserveIncrementDrops) = reserveIncrementDropsValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    FeeSettingsBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    FeeSettings entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = baseFeeValue;

        auto const fromSleOpt = entryFromSle.getBaseFee();
        auto const fromBuilderOpt = entryFromBuilder.getBaseFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBaseFee");
        expectEqualField(expected, *fromBuilderOpt, "sfBaseFee");
    }

    {
        auto const& expected = referenceFeeUnitsValue;

        auto const fromSleOpt = entryFromSle.getReferenceFeeUnits();
        auto const fromBuilderOpt = entryFromBuilder.getReferenceFeeUnits();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfReferenceFeeUnits");
        expectEqualField(expected, *fromBuilderOpt, "sfReferenceFeeUnits");
    }

    {
        auto const& expected = reserveBaseValue;

        auto const fromSleOpt = entryFromSle.getReserveBase();
        auto const fromBuilderOpt = entryFromBuilder.getReserveBase();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfReserveBase");
        expectEqualField(expected, *fromBuilderOpt, "sfReserveBase");
    }

    {
        auto const& expected = reserveIncrementValue;

        auto const fromSleOpt = entryFromSle.getReserveIncrement();
        auto const fromBuilderOpt = entryFromBuilder.getReserveIncrement();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfReserveIncrement");
        expectEqualField(expected, *fromBuilderOpt, "sfReserveIncrement");
    }

    {
        auto const& expected = baseFeeDropsValue;

        auto const fromSleOpt = entryFromSle.getBaseFeeDrops();
        auto const fromBuilderOpt = entryFromBuilder.getBaseFeeDrops();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBaseFeeDrops");
        expectEqualField(expected, *fromBuilderOpt, "sfBaseFeeDrops");
    }

    {
        auto const& expected = reserveBaseDropsValue;

        auto const fromSleOpt = entryFromSle.getReserveBaseDrops();
        auto const fromBuilderOpt = entryFromBuilder.getReserveBaseDrops();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfReserveBaseDrops");
        expectEqualField(expected, *fromBuilderOpt, "sfReserveBaseDrops");
    }

    {
        auto const& expected = reserveIncrementDropsValue;

        auto const fromSleOpt = entryFromSle.getReserveIncrementDrops();
        auto const fromBuilderOpt = entryFromBuilder.getReserveIncrementDrops();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfReserveIncrementDrops");
        expectEqualField(expected, *fromBuilderOpt, "sfReserveIncrementDrops");
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
TEST(FeeSettingsTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(FeeSettings{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(FeeSettingsTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(FeeSettingsBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(FeeSettingsTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    FeeSettingsBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasBaseFee());
    EXPECT_FALSE(entry.getBaseFee().has_value());
    EXPECT_FALSE(entry.hasReferenceFeeUnits());
    EXPECT_FALSE(entry.getReferenceFeeUnits().has_value());
    EXPECT_FALSE(entry.hasReserveBase());
    EXPECT_FALSE(entry.getReserveBase().has_value());
    EXPECT_FALSE(entry.hasReserveIncrement());
    EXPECT_FALSE(entry.getReserveIncrement().has_value());
    EXPECT_FALSE(entry.hasBaseFeeDrops());
    EXPECT_FALSE(entry.getBaseFeeDrops().has_value());
    EXPECT_FALSE(entry.hasReserveBaseDrops());
    EXPECT_FALSE(entry.getReserveBaseDrops().has_value());
    EXPECT_FALSE(entry.hasReserveIncrementDrops());
    EXPECT_FALSE(entry.getReserveIncrementDrops().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
