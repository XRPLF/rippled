// Auto-generated unit tests for ledger entry LedgerHashes


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/LedgerHashes.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(LedgerHashesTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const firstLedgerSequenceValue = canonical_UINT32();
    auto const lastLedgerSequenceValue = canonical_UINT32();
    auto const hashesValue = canonical_VECTOR256();

    LedgerHashesBuilder builder{
    };

    builder.setFirstLedgerSequence(firstLedgerSequenceValue);
    builder.setLastLedgerSequence(lastLedgerSequenceValue);
    builder.setHashes(hashesValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = firstLedgerSequenceValue;
        auto const actualOpt = entry.getFirstLedgerSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFirstLedgerSequence");
        EXPECT_TRUE(entry.hasFirstLedgerSequence());
    }

    {
        auto const& expected = lastLedgerSequenceValue;
        auto const actualOpt = entry.getLastLedgerSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLastLedgerSequence");
        EXPECT_TRUE(entry.hasLastLedgerSequence());
    }

    {
        auto const& expected = hashesValue;
        auto const actualOpt = entry.getHashes();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHashes");
        EXPECT_TRUE(entry.hasHashes());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(LedgerHashesTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const firstLedgerSequenceValue = canonical_UINT32();
    auto const lastLedgerSequenceValue = canonical_UINT32();
    auto const hashesValue = canonical_VECTOR256();

    auto sle = std::make_shared<SLE>(LedgerHashes::entryType, index);

    sle->at(sfFirstLedgerSequence) = firstLedgerSequenceValue;
    sle->at(sfLastLedgerSequence) = lastLedgerSequenceValue;
    sle->at(sfHashes) = hashesValue;

    LedgerHashesBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    LedgerHashes entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = firstLedgerSequenceValue;

        auto const fromSleOpt = entryFromSle.getFirstLedgerSequence();
        auto const fromBuilderOpt = entryFromBuilder.getFirstLedgerSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFirstLedgerSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfFirstLedgerSequence");
    }

    {
        auto const& expected = lastLedgerSequenceValue;

        auto const fromSleOpt = entryFromSle.getLastLedgerSequence();
        auto const fromBuilderOpt = entryFromBuilder.getLastLedgerSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLastLedgerSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfLastLedgerSequence");
    }

    {
        auto const& expected = hashesValue;

        auto const fromSleOpt = entryFromSle.getHashes();
        auto const fromBuilderOpt = entryFromBuilder.getHashes();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHashes");
        expectEqualField(expected, *fromBuilderOpt, "sfHashes");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(LedgerHashesTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(LedgerHashes{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(LedgerHashesTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(LedgerHashesBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(LedgerHashesTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    LedgerHashesBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasFirstLedgerSequence());
    EXPECT_FALSE(entry.getFirstLedgerSequence().has_value());
    EXPECT_FALSE(entry.hasLastLedgerSequence());
    EXPECT_FALSE(entry.getLastLedgerSequence().has_value());
    EXPECT_FALSE(entry.hasHashes());
    EXPECT_FALSE(entry.getHashes().has_value());
}
}
