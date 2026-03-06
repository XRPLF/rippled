// Auto-generated unit tests for ledger entry XChainOwnedClaimID

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/XChainOwnedClaimID.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(XChainOwnedClaimIDTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const otherChainSourceValue = canonical_ACCOUNT();
    auto const xChainClaimAttestationsValue = canonical_ARRAY();
    auto const signatureRewardValue = canonical_AMOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    XChainOwnedClaimIDBuilder builder{
        accountValue,
        xChainBridgeValue,
        xChainClaimIDValue,
        otherChainSourceValue,
        xChainClaimAttestationsValue,
        signatureRewardValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };


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
        auto const& expected = xChainBridgeValue;
        auto const actual = entry->getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    {
        auto const& expected = xChainClaimIDValue;
        auto const actual = entry->getXChainClaimID();
        expectEqualField(expected, actual, "sfXChainClaimID");
    }

    {
        auto const& expected = otherChainSourceValue;
        auto const actual = entry->getOtherChainSource();
        expectEqualField(expected, actual, "sfOtherChainSource");
    }

    {
        auto const& expected = xChainClaimAttestationsValue;
        auto const actual = entry->getXChainClaimAttestations();
        expectEqualField(expected, actual, "sfXChainClaimAttestations");
    }

    {
        auto const& expected = signatureRewardValue;
        auto const actual = entry->getSignatureReward();
        expectEqualField(expected, actual, "sfSignatureReward");
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

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(XChainOwnedClaimIDTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const otherChainSourceValue = canonical_ACCOUNT();
    auto const xChainClaimAttestationsValue = canonical_ARRAY();
    auto const signatureRewardValue = canonical_AMOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    SLE sle{XChainOwnedClaimID::entryType, index};

    sle[sfAccount] = accountValue;
    sle[sfXChainBridge] = xChainBridgeValue;
    sle[sfXChainClaimID] = xChainClaimIDValue;
    sle[sfOtherChainSource] = otherChainSourceValue;
    sle.setFieldArray(sfXChainClaimAttestations, xChainClaimAttestationsValue);
    sle[sfSignatureReward] = signatureRewardValue;
    sle[sfOwnerNode] = ownerNodeValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;

    XChainOwnedClaimIDBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    XChainOwnedClaimID entryFromSle{sle};
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
        auto const& expected = xChainBridgeValue;

        auto const fromSle = entryFromSle.getXChainBridge();
        auto const fromBuilder = entryFromBuilder->getXChainBridge();

        expectEqualField(expected, fromSle, "sfXChainBridge");
        expectEqualField(expected, fromBuilder, "sfXChainBridge");
    }

    {
        auto const& expected = xChainClaimIDValue;

        auto const fromSle = entryFromSle.getXChainClaimID();
        auto const fromBuilder = entryFromBuilder->getXChainClaimID();

        expectEqualField(expected, fromSle, "sfXChainClaimID");
        expectEqualField(expected, fromBuilder, "sfXChainClaimID");
    }

    {
        auto const& expected = otherChainSourceValue;

        auto const fromSle = entryFromSle.getOtherChainSource();
        auto const fromBuilder = entryFromBuilder->getOtherChainSource();

        expectEqualField(expected, fromSle, "sfOtherChainSource");
        expectEqualField(expected, fromBuilder, "sfOtherChainSource");
    }

    {
        auto const& expected = xChainClaimAttestationsValue;

        auto const fromSle = entryFromSle.getXChainClaimAttestations();
        auto const fromBuilder = entryFromBuilder->getXChainClaimAttestations();

        expectEqualField(expected, fromSle, "sfXChainClaimAttestations");
        expectEqualField(expected, fromBuilder, "sfXChainClaimAttestations");
    }

    {
        auto const& expected = signatureRewardValue;

        auto const fromSle = entryFromSle.getSignatureReward();
        auto const fromBuilder = entryFromBuilder->getSignatureReward();

        expectEqualField(expected, fromSle, "sfSignatureReward");
        expectEqualField(expected, fromBuilder, "sfSignatureReward");
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

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
