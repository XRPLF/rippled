// Auto-generated unit tests for ledger entry MarginAccount


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/MarginAccount.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(MarginAccountTests, BuilderSettersRoundTrip)
{
    UInt256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const exchangeIDValue = canonical_UINT256();
    auto const cashValue = canonical_NUMBER();
    auto const waterfallCreditValue = canonical_NUMBER();
    auto const marketsValue = canonical_VECTOR256();
    auto const ownerNodeValue = canonical_UINT64();

    MarginAccountBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        accountValue,
        exchangeIDValue,
        ownerNodeValue
    };

    builder.setCash(cashValue);
    builder.setWaterfallCredit(waterfallCreditValue);
    builder.setMarkets(marketsValue);

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
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = exchangeIDValue;
        auto const actual = entry.getExchangeID();
        expectEqualField(expected, actual, "sfExchangeID");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = cashValue;
        auto const actualOpt = entry.getCash();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCash");
        EXPECT_TRUE(entry.hasCash());
    }

    {
        auto const& expected = waterfallCreditValue;
        auto const actualOpt = entry.getWaterfallCredit();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfWaterfallCredit");
        EXPECT_TRUE(entry.hasWaterfallCredit());
    }

    {
        auto const& expected = marketsValue;
        auto const actualOpt = entry.getMarkets();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMarkets");
        EXPECT_TRUE(entry.hasMarkets());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(MarginAccountTests, BuilderFromSleRoundTrip)
{
    UInt256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const exchangeIDValue = canonical_UINT256();
    auto const cashValue = canonical_NUMBER();
    auto const waterfallCreditValue = canonical_NUMBER();
    auto const marketsValue = canonical_VECTOR256();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(MarginAccount::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfExchangeID) = exchangeIDValue;
    sle->at(sfCash) = cashValue;
    sle->at(sfWaterfallCredit) = waterfallCreditValue;
    sle->at(sfMarkets) = marketsValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    MarginAccountBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    MarginAccount entryFromSle{sle};
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
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = exchangeIDValue;

        auto const fromSle = entryFromSle.getExchangeID();
        auto const fromBuilder = entryFromBuilder.getExchangeID();

        expectEqualField(expected, fromSle, "sfExchangeID");
        expectEqualField(expected, fromBuilder, "sfExchangeID");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = cashValue;

        auto const fromSleOpt = entryFromSle.getCash();
        auto const fromBuilderOpt = entryFromBuilder.getCash();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCash");
        expectEqualField(expected, *fromBuilderOpt, "sfCash");
    }

    {
        auto const& expected = waterfallCreditValue;

        auto const fromSleOpt = entryFromSle.getWaterfallCredit();
        auto const fromBuilderOpt = entryFromBuilder.getWaterfallCredit();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfWaterfallCredit");
        expectEqualField(expected, *fromBuilderOpt, "sfWaterfallCredit");
    }

    {
        auto const& expected = marketsValue;

        auto const fromSleOpt = entryFromSle.getMarkets();
        auto const fromBuilderOpt = entryFromBuilder.getMarkets();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMarkets");
        expectEqualField(expected, *fromBuilderOpt, "sfMarkets");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(MarginAccountTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(MarginAccount{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(MarginAccountTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(MarginAccountBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(MarginAccountTests, OptionalFieldsReturnNullopt)
{
    UInt256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const exchangeIDValue = canonical_UINT256();
    auto const ownerNodeValue = canonical_UINT64();

    MarginAccountBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        accountValue,
        exchangeIDValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasCash());
    EXPECT_FALSE(entry.getCash().has_value());
    EXPECT_FALSE(entry.hasWaterfallCredit());
    EXPECT_FALSE(entry.getWaterfallCredit().has_value());
    EXPECT_FALSE(entry.hasMarkets());
    EXPECT_FALSE(entry.getMarkets().has_value());
}
}
