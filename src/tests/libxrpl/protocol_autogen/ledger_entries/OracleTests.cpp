// Auto-generated unit tests for ledger entry Oracle


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Oracle.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(OracleTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const oracleDocumentIDValue = canonical_UINT32();
    auto const providerValue = canonical_VL();
    auto const priceDataSeriesValue = canonical_ARRAY();
    auto const assetClassValue = canonical_VL();
    auto const lastUpdateTimeValue = canonical_UINT32();
    auto const uRIValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    OracleBuilder builder{
        ownerValue,
        providerValue,
        priceDataSeriesValue,
        assetClassValue,
        lastUpdateTimeValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setOracleDocumentID(oracleDocumentIDValue);
    builder.setURI(uRIValue);

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
        auto const& expected = providerValue;
        auto const actual = entry.getProvider();
        expectEqualField(expected, actual, "sfProvider");
    }

    {
        auto const& expected = priceDataSeriesValue;
        auto const actual = entry.getPriceDataSeries();
        expectEqualField(expected, actual, "sfPriceDataSeries");
    }

    {
        auto const& expected = assetClassValue;
        auto const actual = entry.getAssetClass();
        expectEqualField(expected, actual, "sfAssetClass");
    }

    {
        auto const& expected = lastUpdateTimeValue;
        auto const actual = entry.getLastUpdateTime();
        expectEqualField(expected, actual, "sfLastUpdateTime");
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
        auto const& expected = oracleDocumentIDValue;
        auto const actualOpt = entry.getOracleDocumentID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOracleDocumentID");
        EXPECT_TRUE(entry.hasOracleDocumentID());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = entry.getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry.hasURI());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(OracleTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const oracleDocumentIDValue = canonical_UINT32();
    auto const providerValue = canonical_VL();
    auto const priceDataSeriesValue = canonical_ARRAY();
    auto const assetClassValue = canonical_VL();
    auto const lastUpdateTimeValue = canonical_UINT32();
    auto const uRIValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(Oracle::entryType, index);

    sle->at(sfOwner) = ownerValue;
    sle->at(sfOracleDocumentID) = oracleDocumentIDValue;
    sle->at(sfProvider) = providerValue;
    sle->setFieldArray(sfPriceDataSeries, priceDataSeriesValue);
    sle->at(sfAssetClass) = assetClassValue;
    sle->at(sfLastUpdateTime) = lastUpdateTimeValue;
    sle->at(sfURI) = uRIValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    OracleBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Oracle entryFromSle{sle};
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
        auto const& expected = providerValue;

        auto const fromSle = entryFromSle.getProvider();
        auto const fromBuilder = entryFromBuilder.getProvider();

        expectEqualField(expected, fromSle, "sfProvider");
        expectEqualField(expected, fromBuilder, "sfProvider");
    }

    {
        auto const& expected = priceDataSeriesValue;

        auto const fromSle = entryFromSle.getPriceDataSeries();
        auto const fromBuilder = entryFromBuilder.getPriceDataSeries();

        expectEqualField(expected, fromSle, "sfPriceDataSeries");
        expectEqualField(expected, fromBuilder, "sfPriceDataSeries");
    }

    {
        auto const& expected = assetClassValue;

        auto const fromSle = entryFromSle.getAssetClass();
        auto const fromBuilder = entryFromBuilder.getAssetClass();

        expectEqualField(expected, fromSle, "sfAssetClass");
        expectEqualField(expected, fromBuilder, "sfAssetClass");
    }

    {
        auto const& expected = lastUpdateTimeValue;

        auto const fromSle = entryFromSle.getLastUpdateTime();
        auto const fromBuilder = entryFromBuilder.getLastUpdateTime();

        expectEqualField(expected, fromSle, "sfLastUpdateTime");
        expectEqualField(expected, fromBuilder, "sfLastUpdateTime");
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
        auto const& expected = oracleDocumentIDValue;

        auto const fromSleOpt = entryFromSle.getOracleDocumentID();
        auto const fromBuilderOpt = entryFromBuilder.getOracleDocumentID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOracleDocumentID");
        expectEqualField(expected, *fromBuilderOpt, "sfOracleDocumentID");
    }

    {
        auto const& expected = uRIValue;

        auto const fromSleOpt = entryFromSle.getURI();
        auto const fromBuilderOpt = entryFromBuilder.getURI();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfURI");
        expectEqualField(expected, *fromBuilderOpt, "sfURI");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(OracleTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Oracle{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(OracleTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(OracleBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(OracleTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const providerValue = canonical_VL();
    auto const priceDataSeriesValue = canonical_ARRAY();
    auto const assetClassValue = canonical_VL();
    auto const lastUpdateTimeValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    OracleBuilder builder{
        ownerValue,
        providerValue,
        priceDataSeriesValue,
        assetClassValue,
        lastUpdateTimeValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasOracleDocumentID());
    EXPECT_FALSE(entry.getOracleDocumentID().has_value());
    EXPECT_FALSE(entry.hasURI());
    EXPECT_FALSE(entry.getURI().has_value());
}
}
