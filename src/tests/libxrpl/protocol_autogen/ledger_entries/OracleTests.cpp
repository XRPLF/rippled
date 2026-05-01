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
    };

    builder.setOwner(ownerValue);
    builder.setOracleDocumentID(oracleDocumentIDValue);
    builder.setProvider(providerValue);
    builder.setPriceDataSeries(priceDataSeriesValue);
    builder.setAssetClass(assetClassValue);
    builder.setLastUpdateTime(lastUpdateTimeValue);
    builder.setURI(uRIValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = ownerValue;
        auto const actualOpt = entry.getOwner();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(entry.hasOwner());
    }

    {
        auto const& expected = oracleDocumentIDValue;
        auto const actualOpt = entry.getOracleDocumentID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOracleDocumentID");
        EXPECT_TRUE(entry.hasOracleDocumentID());
    }

    {
        auto const& expected = providerValue;
        auto const actualOpt = entry.getProvider();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfProvider");
        EXPECT_TRUE(entry.hasProvider());
    }

    {
        auto const& expected = priceDataSeriesValue;
        auto const actualOpt = entry.getPriceDataSeries();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPriceDataSeries");
        EXPECT_TRUE(entry.hasPriceDataSeries());
    }

    {
        auto const& expected = assetClassValue;
        auto const actualOpt = entry.getAssetClass();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAssetClass");
        EXPECT_TRUE(entry.hasAssetClass());
    }

    {
        auto const& expected = lastUpdateTimeValue;
        auto const actualOpt = entry.getLastUpdateTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLastUpdateTime");
        EXPECT_TRUE(entry.hasLastUpdateTime());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = entry.getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry.hasURI());
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

        auto const fromSleOpt = entryFromSle.getOwner();
        auto const fromBuilderOpt = entryFromBuilder.getOwner();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwner");
        expectEqualField(expected, *fromBuilderOpt, "sfOwner");
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
        auto const& expected = providerValue;

        auto const fromSleOpt = entryFromSle.getProvider();
        auto const fromBuilderOpt = entryFromBuilder.getProvider();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfProvider");
        expectEqualField(expected, *fromBuilderOpt, "sfProvider");
    }

    {
        auto const& expected = priceDataSeriesValue;

        auto const fromSleOpt = entryFromSle.getPriceDataSeries();
        auto const fromBuilderOpt = entryFromBuilder.getPriceDataSeries();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPriceDataSeries");
        expectEqualField(expected, *fromBuilderOpt, "sfPriceDataSeries");
    }

    {
        auto const& expected = assetClassValue;

        auto const fromSleOpt = entryFromSle.getAssetClass();
        auto const fromBuilderOpt = entryFromBuilder.getAssetClass();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAssetClass");
        expectEqualField(expected, *fromBuilderOpt, "sfAssetClass");
    }

    {
        auto const& expected = lastUpdateTimeValue;

        auto const fromSleOpt = entryFromSle.getLastUpdateTime();
        auto const fromBuilderOpt = entryFromBuilder.getLastUpdateTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLastUpdateTime");
        expectEqualField(expected, *fromBuilderOpt, "sfLastUpdateTime");
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


    OracleBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasOwner());
    EXPECT_FALSE(entry.getOwner().has_value());
    EXPECT_FALSE(entry.hasOracleDocumentID());
    EXPECT_FALSE(entry.getOracleDocumentID().has_value());
    EXPECT_FALSE(entry.hasProvider());
    EXPECT_FALSE(entry.getProvider().has_value());
    EXPECT_FALSE(entry.hasPriceDataSeries());
    EXPECT_FALSE(entry.getPriceDataSeries().has_value());
    EXPECT_FALSE(entry.hasAssetClass());
    EXPECT_FALSE(entry.getAssetClass().has_value());
    EXPECT_FALSE(entry.hasLastUpdateTime());
    EXPECT_FALSE(entry.getLastUpdateTime().has_value());
    EXPECT_FALSE(entry.hasURI());
    EXPECT_FALSE(entry.getURI().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
