// Auto-generated unit tests for ledger entry DID

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/DID.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(DIDTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const dIDDocumentValue = canonical_VL();
    auto const uRIValue = canonical_VL();
    auto const dataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    DIDBuilder builder{
        accountValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setDIDDocument(dIDDocumentValue);
    builder.setURI(uRIValue);
    builder.setData(dataValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry->validate());

    {
        auto const& expected = accountValue;
        auto const actual = entry->getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry->getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = previousTxnIDValue;
        auto const actual = entry->getPreviousTxnID();
        expectEqualField(expected, actual, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;
        auto const actual = entry->getPreviousTxnLgrSeq();
        expectEqualField(expected, actual, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = dIDDocumentValue;
        auto const actualOpt = entry->getDIDDocument();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDIDDocument");
        EXPECT_TRUE(entry->hasDIDDocument());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = entry->getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry->hasURI());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = entry->getData();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(entry->hasData());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(DIDTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const dIDDocumentValue = canonical_VL();
    auto const uRIValue = canonical_VL();
    auto const dataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    SLE sle{DID::entryType, index};

    sle[sfAccount] = accountValue;
    sle[sfDIDDocument] = dIDDocumentValue;
    sle[sfURI] = uRIValue;
    sle[sfData] = dataValue;
    sle[sfOwnerNode] = ownerNodeValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;

    DIDBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    DID entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder->validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder->getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder->getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = previousTxnIDValue;

        auto const fromSle = entryFromSle.getPreviousTxnID();
        auto const fromBuilder = entryFromBuilder->getPreviousTxnID();

        expectEqualField(expected, fromSle, "sfPreviousTxnID");
        expectEqualField(expected, fromBuilder, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;

        auto const fromSle = entryFromSle.getPreviousTxnLgrSeq();
        auto const fromBuilder = entryFromBuilder->getPreviousTxnLgrSeq();

        expectEqualField(expected, fromSle, "sfPreviousTxnLgrSeq");
        expectEqualField(expected, fromBuilder, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = dIDDocumentValue;

        auto const fromSleOpt = entryFromSle.getDIDDocument();
        auto const fromBuilderOpt = entryFromBuilder->getDIDDocument();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDIDDocument");
        expectEqualField(expected, *fromBuilderOpt, "sfDIDDocument");
    }

    {
        auto const& expected = uRIValue;

        auto const fromSleOpt = entryFromSle.getURI();
        auto const fromBuilderOpt = entryFromBuilder->getURI();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfURI");
        expectEqualField(expected, *fromBuilderOpt, "sfURI");
    }

    {
        auto const& expected = dataValue;

        auto const fromSleOpt = entryFromSle.getData();
        auto const fromBuilderOpt = entryFromBuilder->getData();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfData");
        expectEqualField(expected, *fromBuilderOpt, "sfData");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
