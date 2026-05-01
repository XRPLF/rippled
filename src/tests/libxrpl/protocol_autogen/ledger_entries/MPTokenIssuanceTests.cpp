// Auto-generated unit tests for ledger entry MPTokenIssuance


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/MPTokenIssuance.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(MPTokenIssuanceTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const issuerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const transferFeeValue = canonical_UINT16();
    auto const ownerNodeValue = canonical_UINT64();
    auto const assetScaleValue = canonical_UINT8();
    auto const maximumAmountValue = canonical_UINT64();
    auto const outstandingAmountValue = canonical_UINT64();
    auto const lockedAmountValue = canonical_UINT64();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();
    auto const mutableFlagsValue = canonical_UINT32();

    MPTokenIssuanceBuilder builder{
    };

    builder.setIssuer(issuerValue);
    builder.setSequence(sequenceValue);
    builder.setTransferFee(transferFeeValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setAssetScale(assetScaleValue);
    builder.setMaximumAmount(maximumAmountValue);
    builder.setOutstandingAmount(outstandingAmountValue);
    builder.setLockedAmount(lockedAmountValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);
    builder.setDomainID(domainIDValue);
    builder.setMutableFlags(mutableFlagsValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = issuerValue;
        auto const actualOpt = entry.getIssuer();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuer");
        EXPECT_TRUE(entry.hasIssuer());
    }

    {
        auto const& expected = sequenceValue;
        auto const actualOpt = entry.getSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSequence");
        EXPECT_TRUE(entry.hasSequence());
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = entry.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(entry.hasTransferFee());
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actualOpt = entry.getOwnerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerNode");
        EXPECT_TRUE(entry.hasOwnerNode());
    }

    {
        auto const& expected = assetScaleValue;
        auto const actualOpt = entry.getAssetScale();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAssetScale");
        EXPECT_TRUE(entry.hasAssetScale());
    }

    {
        auto const& expected = maximumAmountValue;
        auto const actualOpt = entry.getMaximumAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMaximumAmount");
        EXPECT_TRUE(entry.hasMaximumAmount());
    }

    {
        auto const& expected = outstandingAmountValue;
        auto const actualOpt = entry.getOutstandingAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOutstandingAmount");
        EXPECT_TRUE(entry.hasOutstandingAmount());
    }

    {
        auto const& expected = lockedAmountValue;
        auto const actualOpt = entry.getLockedAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLockedAmount");
        EXPECT_TRUE(entry.hasLockedAmount());
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = entry.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
        EXPECT_TRUE(entry.hasMPTokenMetadata());
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

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = entry.getDomainID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(entry.hasDomainID());
    }

    {
        auto const& expected = mutableFlagsValue;
        auto const actualOpt = entry.getMutableFlags();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMutableFlags");
        EXPECT_TRUE(entry.hasMutableFlags());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(MPTokenIssuanceTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const issuerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const transferFeeValue = canonical_UINT16();
    auto const ownerNodeValue = canonical_UINT64();
    auto const assetScaleValue = canonical_UINT8();
    auto const maximumAmountValue = canonical_UINT64();
    auto const outstandingAmountValue = canonical_UINT64();
    auto const lockedAmountValue = canonical_UINT64();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();
    auto const mutableFlagsValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(MPTokenIssuance::entryType, index);

    sle->at(sfIssuer) = issuerValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfTransferFee) = transferFeeValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfAssetScale) = assetScaleValue;
    sle->at(sfMaximumAmount) = maximumAmountValue;
    sle->at(sfOutstandingAmount) = outstandingAmountValue;
    sle->at(sfLockedAmount) = lockedAmountValue;
    sle->at(sfMPTokenMetadata) = mPTokenMetadataValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfDomainID) = domainIDValue;
    sle->at(sfMutableFlags) = mutableFlagsValue;

    MPTokenIssuanceBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    MPTokenIssuance entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = issuerValue;

        auto const fromSleOpt = entryFromSle.getIssuer();
        auto const fromBuilderOpt = entryFromBuilder.getIssuer();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuer");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuer");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSleOpt = entryFromSle.getSequence();
        auto const fromBuilderOpt = entryFromBuilder.getSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfSequence");
    }

    {
        auto const& expected = transferFeeValue;

        auto const fromSleOpt = entryFromSle.getTransferFee();
        auto const fromBuilderOpt = entryFromBuilder.getTransferFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTransferFee");
        expectEqualField(expected, *fromBuilderOpt, "sfTransferFee");
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
        auto const& expected = assetScaleValue;

        auto const fromSleOpt = entryFromSle.getAssetScale();
        auto const fromBuilderOpt = entryFromBuilder.getAssetScale();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAssetScale");
        expectEqualField(expected, *fromBuilderOpt, "sfAssetScale");
    }

    {
        auto const& expected = maximumAmountValue;

        auto const fromSleOpt = entryFromSle.getMaximumAmount();
        auto const fromBuilderOpt = entryFromBuilder.getMaximumAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMaximumAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfMaximumAmount");
    }

    {
        auto const& expected = outstandingAmountValue;

        auto const fromSleOpt = entryFromSle.getOutstandingAmount();
        auto const fromBuilderOpt = entryFromBuilder.getOutstandingAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOutstandingAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfOutstandingAmount");
    }

    {
        auto const& expected = lockedAmountValue;

        auto const fromSleOpt = entryFromSle.getLockedAmount();
        auto const fromBuilderOpt = entryFromBuilder.getLockedAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLockedAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfLockedAmount");
    }

    {
        auto const& expected = mPTokenMetadataValue;

        auto const fromSleOpt = entryFromSle.getMPTokenMetadata();
        auto const fromBuilderOpt = entryFromBuilder.getMPTokenMetadata();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMPTokenMetadata");
        expectEqualField(expected, *fromBuilderOpt, "sfMPTokenMetadata");
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

    {
        auto const& expected = domainIDValue;

        auto const fromSleOpt = entryFromSle.getDomainID();
        auto const fromBuilderOpt = entryFromBuilder.getDomainID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDomainID");
        expectEqualField(expected, *fromBuilderOpt, "sfDomainID");
    }

    {
        auto const& expected = mutableFlagsValue;

        auto const fromSleOpt = entryFromSle.getMutableFlags();
        auto const fromBuilderOpt = entryFromBuilder.getMutableFlags();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMutableFlags");
        expectEqualField(expected, *fromBuilderOpt, "sfMutableFlags");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(MPTokenIssuanceTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(MPTokenIssuance{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(MPTokenIssuanceTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(MPTokenIssuanceBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(MPTokenIssuanceTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    MPTokenIssuanceBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasIssuer());
    EXPECT_FALSE(entry.getIssuer().has_value());
    EXPECT_FALSE(entry.hasSequence());
    EXPECT_FALSE(entry.getSequence().has_value());
    EXPECT_FALSE(entry.hasTransferFee());
    EXPECT_FALSE(entry.getTransferFee().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasAssetScale());
    EXPECT_FALSE(entry.getAssetScale().has_value());
    EXPECT_FALSE(entry.hasMaximumAmount());
    EXPECT_FALSE(entry.getMaximumAmount().has_value());
    EXPECT_FALSE(entry.hasOutstandingAmount());
    EXPECT_FALSE(entry.getOutstandingAmount().has_value());
    EXPECT_FALSE(entry.hasLockedAmount());
    EXPECT_FALSE(entry.getLockedAmount().has_value());
    EXPECT_FALSE(entry.hasMPTokenMetadata());
    EXPECT_FALSE(entry.getMPTokenMetadata().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
    EXPECT_FALSE(entry.hasDomainID());
    EXPECT_FALSE(entry.getDomainID().has_value());
    EXPECT_FALSE(entry.hasMutableFlags());
    EXPECT_FALSE(entry.getMutableFlags().has_value());
}
}
