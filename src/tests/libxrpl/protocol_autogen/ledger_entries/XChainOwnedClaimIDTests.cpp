// Auto-generated unit tests for ledger entry XChainOwnedClaimID


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/XChainOwnedClaimID.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

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
    };

    builder.setAccount(accountValue);
    builder.setXChainBridge(xChainBridgeValue);
    builder.setXChainClaimID(xChainClaimIDValue);
    builder.setOtherChainSource(otherChainSourceValue);
    builder.setXChainClaimAttestations(xChainClaimAttestationsValue);
    builder.setSignatureReward(signatureRewardValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = accountValue;
        auto const actualOpt = entry.getAccount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAccount");
        EXPECT_TRUE(entry.hasAccount());
    }

    {
        auto const& expected = xChainBridgeValue;
        auto const actualOpt = entry.getXChainBridge();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfXChainBridge");
        EXPECT_TRUE(entry.hasXChainBridge());
    }

    {
        auto const& expected = xChainClaimIDValue;
        auto const actualOpt = entry.getXChainClaimID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfXChainClaimID");
        EXPECT_TRUE(entry.hasXChainClaimID());
    }

    {
        auto const& expected = otherChainSourceValue;
        auto const actualOpt = entry.getOtherChainSource();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOtherChainSource");
        EXPECT_TRUE(entry.hasOtherChainSource());
    }

    {
        auto const& expected = xChainClaimAttestationsValue;
        auto const actualOpt = entry.getXChainClaimAttestations();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfXChainClaimAttestations");
        EXPECT_TRUE(entry.hasXChainClaimAttestations());
    }

    {
        auto const& expected = signatureRewardValue;
        auto const actualOpt = entry.getSignatureReward();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSignatureReward");
        EXPECT_TRUE(entry.hasSignatureReward());
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

    auto sle = std::make_shared<SLE>(XChainOwnedClaimID::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfXChainBridge) = xChainBridgeValue;
    sle->at(sfXChainClaimID) = xChainClaimIDValue;
    sle->at(sfOtherChainSource) = otherChainSourceValue;
    sle->setFieldArray(sfXChainClaimAttestations, xChainClaimAttestationsValue);
    sle->at(sfSignatureReward) = signatureRewardValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    XChainOwnedClaimIDBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    XChainOwnedClaimID entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSleOpt = entryFromSle.getAccount();
        auto const fromBuilderOpt = entryFromBuilder.getAccount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAccount");
        expectEqualField(expected, *fromBuilderOpt, "sfAccount");
    }

    {
        auto const& expected = xChainBridgeValue;

        auto const fromSleOpt = entryFromSle.getXChainBridge();
        auto const fromBuilderOpt = entryFromBuilder.getXChainBridge();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfXChainBridge");
        expectEqualField(expected, *fromBuilderOpt, "sfXChainBridge");
    }

    {
        auto const& expected = xChainClaimIDValue;

        auto const fromSleOpt = entryFromSle.getXChainClaimID();
        auto const fromBuilderOpt = entryFromBuilder.getXChainClaimID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfXChainClaimID");
        expectEqualField(expected, *fromBuilderOpt, "sfXChainClaimID");
    }

    {
        auto const& expected = otherChainSourceValue;

        auto const fromSleOpt = entryFromSle.getOtherChainSource();
        auto const fromBuilderOpt = entryFromBuilder.getOtherChainSource();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOtherChainSource");
        expectEqualField(expected, *fromBuilderOpt, "sfOtherChainSource");
    }

    {
        auto const& expected = xChainClaimAttestationsValue;

        auto const fromSleOpt = entryFromSle.getXChainClaimAttestations();
        auto const fromBuilderOpt = entryFromBuilder.getXChainClaimAttestations();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfXChainClaimAttestations");
        expectEqualField(expected, *fromBuilderOpt, "sfXChainClaimAttestations");
    }

    {
        auto const& expected = signatureRewardValue;

        auto const fromSleOpt = entryFromSle.getSignatureReward();
        auto const fromBuilderOpt = entryFromBuilder.getSignatureReward();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSignatureReward");
        expectEqualField(expected, *fromBuilderOpt, "sfSignatureReward");
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
TEST(XChainOwnedClaimIDTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(XChainOwnedClaimID{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(XChainOwnedClaimIDTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(XChainOwnedClaimIDBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(XChainOwnedClaimIDTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    XChainOwnedClaimIDBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAccount());
    EXPECT_FALSE(entry.getAccount().has_value());
    EXPECT_FALSE(entry.hasXChainBridge());
    EXPECT_FALSE(entry.getXChainBridge().has_value());
    EXPECT_FALSE(entry.hasXChainClaimID());
    EXPECT_FALSE(entry.getXChainClaimID().has_value());
    EXPECT_FALSE(entry.hasOtherChainSource());
    EXPECT_FALSE(entry.getOtherChainSource().has_value());
    EXPECT_FALSE(entry.hasXChainClaimAttestations());
    EXPECT_FALSE(entry.getXChainClaimAttestations().has_value());
    EXPECT_FALSE(entry.hasSignatureReward());
    EXPECT_FALSE(entry.getSignatureReward().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
