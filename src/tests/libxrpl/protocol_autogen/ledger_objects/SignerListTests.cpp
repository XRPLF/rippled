// Auto-generated unit tests for ledger entry SignerList

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/SignerList.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(SignerListTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const signerQuorumValue = canonical_UINT32();
    auto const signerEntriesValue = canonical_ARRAY();
    auto const signerListIDValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    SignerListBuilder builder{
        ownerNodeValue,
        signerQuorumValue,
        signerEntriesValue,
        signerListIDValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setOwner(ownerValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry->validate());

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry->getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = signerQuorumValue;
        auto const actual = entry->getSignerQuorum();
        expectEqualField(expected, actual, "sfSignerQuorum");
    }

    {
        auto const& expected = signerEntriesValue;
        auto const actual = entry->getSignerEntries();
        expectEqualField(expected, actual, "sfSignerEntries");
    }

    {
        auto const& expected = signerListIDValue;
        auto const actual = entry->getSignerListID();
        expectEqualField(expected, actual, "sfSignerListID");
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
        auto const& expected = ownerValue;
        auto const actualOpt = entry->getOwner();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(entry->hasOwner());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(SignerListTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const signerQuorumValue = canonical_UINT32();
    auto const signerEntriesValue = canonical_ARRAY();
    auto const signerListIDValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    SLE sle{SignerList::entryType, index};

    sle[sfOwner] = ownerValue;
    sle[sfOwnerNode] = ownerNodeValue;
    sle[sfSignerQuorum] = signerQuorumValue;
    sle.setFieldArray(sfSignerEntries, signerEntriesValue);
    sle[sfSignerListID] = signerListIDValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;

    SignerListBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    SignerList entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder->validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder->getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = signerQuorumValue;

        auto const fromSle = entryFromSle.getSignerQuorum();
        auto const fromBuilder = entryFromBuilder->getSignerQuorum();

        expectEqualField(expected, fromSle, "sfSignerQuorum");
        expectEqualField(expected, fromBuilder, "sfSignerQuorum");
    }

    {
        auto const& expected = signerEntriesValue;

        auto const fromSle = entryFromSle.getSignerEntries();
        auto const fromBuilder = entryFromBuilder->getSignerEntries();

        expectEqualField(expected, fromSle, "sfSignerEntries");
        expectEqualField(expected, fromBuilder, "sfSignerEntries");
    }

    {
        auto const& expected = signerListIDValue;

        auto const fromSle = entryFromSle.getSignerListID();
        auto const fromBuilder = entryFromBuilder->getSignerListID();

        expectEqualField(expected, fromSle, "sfSignerListID");
        expectEqualField(expected, fromBuilder, "sfSignerListID");
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
        auto const& expected = ownerValue;

        auto const fromSleOpt = entryFromSle.getOwner();
        auto const fromBuilderOpt = entryFromBuilder->getOwner();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwner");
        expectEqualField(expected, *fromBuilderOpt, "sfOwner");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
