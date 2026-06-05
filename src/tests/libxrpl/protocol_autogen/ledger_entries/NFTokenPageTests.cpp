// Auto-generated unit tests for ledger entry NFTokenPage


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/NFTokenPage.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(NFTokenPageTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousPageMinValue = canonical_UINT256();
    auto const nextPageMinValue = canonical_UINT256();
    auto const nFTokensValue = canonical_ARRAY();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    NFTokenPageBuilder builder{
        nFTokensValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setPreviousPageMin(previousPageMinValue);
    builder.setNextPageMin(nextPageMinValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = nFTokensValue;
        auto const actual = entry.getNFTokens();
        expectEqualField(expected, actual, "sfNFTokens");
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
        auto const& expected = previousPageMinValue;
        auto const actualOpt = entry.getPreviousPageMin();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousPageMin");
        EXPECT_TRUE(entry.hasPreviousPageMin());
    }

    {
        auto const& expected = nextPageMinValue;
        auto const actualOpt = entry.getNextPageMin();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfNextPageMin");
        EXPECT_TRUE(entry.hasNextPageMin());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(NFTokenPageTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousPageMinValue = canonical_UINT256();
    auto const nextPageMinValue = canonical_UINT256();
    auto const nFTokensValue = canonical_ARRAY();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(NFTokenPage::entryType, index);

    sle->at(sfPreviousPageMin) = previousPageMinValue;
    sle->at(sfNextPageMin) = nextPageMinValue;
    sle->setFieldArray(sfNFTokens, nFTokensValue);
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    NFTokenPageBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    NFTokenPage entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = nFTokensValue;

        auto const fromSle = entryFromSle.getNFTokens();
        auto const fromBuilder = entryFromBuilder.getNFTokens();

        expectEqualField(expected, fromSle, "sfNFTokens");
        expectEqualField(expected, fromBuilder, "sfNFTokens");
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
        auto const& expected = previousPageMinValue;

        auto const fromSleOpt = entryFromSle.getPreviousPageMin();
        auto const fromBuilderOpt = entryFromBuilder.getPreviousPageMin();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousPageMin");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousPageMin");
    }

    {
        auto const& expected = nextPageMinValue;

        auto const fromSleOpt = entryFromSle.getNextPageMin();
        auto const fromBuilderOpt = entryFromBuilder.getNextPageMin();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfNextPageMin");
        expectEqualField(expected, *fromBuilderOpt, "sfNextPageMin");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(NFTokenPageTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(NFTokenPage{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(NFTokenPageTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(NFTokenPageBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(NFTokenPageTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const nFTokensValue = canonical_ARRAY();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    NFTokenPageBuilder builder{
        nFTokensValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasPreviousPageMin());
    EXPECT_FALSE(entry.getPreviousPageMin().has_value());
    EXPECT_FALSE(entry.hasNextPageMin());
    EXPECT_FALSE(entry.getNextPageMin().has_value());
}
}
