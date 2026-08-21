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
    auto const immutableFlagsValue = canonical_UINT32();
    auto const referenceHoldingValue = canonical_UINT256();
    auto const issuerEncryptionKeyValue = canonical_VL();
    auto const auditorEncryptionKeyValue = canonical_VL();
    auto const issuerKeyEpochValue = canonical_UINT32();
    auto const auditorKeyEpochValue = canonical_UINT32();
    auto const holderCountValue = canonical_UINT64();
    auto const confidentialOutstandingAmountValue = canonical_UINT64();

    MPTokenIssuanceBuilder builder{
        issuerValue,
        sequenceValue,
        ownerNodeValue,
        outstandingAmountValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setTransferFee(transferFeeValue);
    builder.setAssetScale(assetScaleValue);
    builder.setMaximumAmount(maximumAmountValue);
    builder.setLockedAmount(lockedAmountValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);
    builder.setDomainID(domainIDValue);
    builder.setImmutableFlags(immutableFlagsValue);
    builder.setReferenceHolding(referenceHoldingValue);
    builder.setIssuerEncryptionKey(issuerEncryptionKeyValue);
    builder.setAuditorEncryptionKey(auditorEncryptionKeyValue);
    builder.setIssuerKeyEpoch(issuerKeyEpochValue);
    builder.setAuditorKeyEpoch(auditorKeyEpochValue);
    builder.setHolderCount(holderCountValue);
    builder.setConfidentialOutstandingAmount(confidentialOutstandingAmountValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = issuerValue;
        auto const actual = entry.getIssuer();
        expectEqualField(expected, actual, "sfIssuer");
    }

    {
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = outstandingAmountValue;
        auto const actual = entry.getOutstandingAmount();
        expectEqualField(expected, actual, "sfOutstandingAmount");
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
        auto const& expected = transferFeeValue;
        auto const actualOpt = entry.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(entry.hasTransferFee());
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
        auto const& expected = domainIDValue;
        auto const actualOpt = entry.getDomainID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(entry.hasDomainID());
    }

    {
        auto const& expected = immutableFlagsValue;
        auto const actualOpt = entry.getImmutableFlags();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfImmutableFlags");
        EXPECT_TRUE(entry.hasImmutableFlags());
    }

    {
        auto const& expected = referenceHoldingValue;
        auto const actualOpt = entry.getReferenceHolding();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfReferenceHolding");
        EXPECT_TRUE(entry.hasReferenceHolding());
    }

    {
        auto const& expected = issuerEncryptionKeyValue;
        auto const actualOpt = entry.getIssuerEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuerEncryptionKey");
        EXPECT_TRUE(entry.hasIssuerEncryptionKey());
    }

    {
        auto const& expected = auditorEncryptionKeyValue;
        auto const actualOpt = entry.getAuditorEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptionKey");
        EXPECT_TRUE(entry.hasAuditorEncryptionKey());
    }

    {
        auto const& expected = issuerKeyEpochValue;
        auto const actualOpt = entry.getIssuerKeyEpoch();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuerKeyEpoch");
        EXPECT_TRUE(entry.hasIssuerKeyEpoch());
    }

    {
        auto const& expected = auditorKeyEpochValue;
        auto const actualOpt = entry.getAuditorKeyEpoch();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuditorKeyEpoch");
        EXPECT_TRUE(entry.hasAuditorKeyEpoch());
    }

    {
        auto const& expected = holderCountValue;
        auto const actualOpt = entry.getHolderCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHolderCount");
        EXPECT_TRUE(entry.hasHolderCount());
    }

    {
        auto const& expected = confidentialOutstandingAmountValue;
        auto const actualOpt = entry.getConfidentialOutstandingAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfConfidentialOutstandingAmount");
        EXPECT_TRUE(entry.hasConfidentialOutstandingAmount());
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
    auto const immutableFlagsValue = canonical_UINT32();
    auto const referenceHoldingValue = canonical_UINT256();
    auto const issuerEncryptionKeyValue = canonical_VL();
    auto const auditorEncryptionKeyValue = canonical_VL();
    auto const issuerKeyEpochValue = canonical_UINT32();
    auto const auditorKeyEpochValue = canonical_UINT32();
    auto const holderCountValue = canonical_UINT64();
    auto const confidentialOutstandingAmountValue = canonical_UINT64();

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
    sle->at(sfImmutableFlags) = immutableFlagsValue;
    sle->at(sfReferenceHolding) = referenceHoldingValue;
    sle->at(sfIssuerEncryptionKey) = issuerEncryptionKeyValue;
    sle->at(sfAuditorEncryptionKey) = auditorEncryptionKeyValue;
    sle->at(sfIssuerKeyEpoch) = issuerKeyEpochValue;
    sle->at(sfAuditorKeyEpoch) = auditorKeyEpochValue;
    sle->at(sfHolderCount) = holderCountValue;
    sle->at(sfConfidentialOutstandingAmount) = confidentialOutstandingAmountValue;

    MPTokenIssuanceBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    MPTokenIssuance entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = issuerValue;

        auto const fromSle = entryFromSle.getIssuer();
        auto const fromBuilder = entryFromBuilder.getIssuer();

        expectEqualField(expected, fromSle, "sfIssuer");
        expectEqualField(expected, fromBuilder, "sfIssuer");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = outstandingAmountValue;

        auto const fromSle = entryFromSle.getOutstandingAmount();
        auto const fromBuilder = entryFromBuilder.getOutstandingAmount();

        expectEqualField(expected, fromSle, "sfOutstandingAmount");
        expectEqualField(expected, fromBuilder, "sfOutstandingAmount");
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
        auto const& expected = transferFeeValue;

        auto const fromSleOpt = entryFromSle.getTransferFee();
        auto const fromBuilderOpt = entryFromBuilder.getTransferFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTransferFee");
        expectEqualField(expected, *fromBuilderOpt, "sfTransferFee");
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
        auto const& expected = domainIDValue;

        auto const fromSleOpt = entryFromSle.getDomainID();
        auto const fromBuilderOpt = entryFromBuilder.getDomainID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDomainID");
        expectEqualField(expected, *fromBuilderOpt, "sfDomainID");
    }

    {
        auto const& expected = immutableFlagsValue;

        auto const fromSleOpt = entryFromSle.getImmutableFlags();
        auto const fromBuilderOpt = entryFromBuilder.getImmutableFlags();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfImmutableFlags");
        expectEqualField(expected, *fromBuilderOpt, "sfImmutableFlags");
    }

    {
        auto const& expected = referenceHoldingValue;

        auto const fromSleOpt = entryFromSle.getReferenceHolding();
        auto const fromBuilderOpt = entryFromBuilder.getReferenceHolding();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfReferenceHolding");
        expectEqualField(expected, *fromBuilderOpt, "sfReferenceHolding");
    }

    {
        auto const& expected = issuerEncryptionKeyValue;

        auto const fromSleOpt = entryFromSle.getIssuerEncryptionKey();
        auto const fromBuilderOpt = entryFromBuilder.getIssuerEncryptionKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuerEncryptionKey");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuerEncryptionKey");
    }

    {
        auto const& expected = auditorEncryptionKeyValue;

        auto const fromSleOpt = entryFromSle.getAuditorEncryptionKey();
        auto const fromBuilderOpt = entryFromBuilder.getAuditorEncryptionKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuditorEncryptionKey");
        expectEqualField(expected, *fromBuilderOpt, "sfAuditorEncryptionKey");
    }

    {
        auto const& expected = issuerKeyEpochValue;

        auto const fromSleOpt = entryFromSle.getIssuerKeyEpoch();
        auto const fromBuilderOpt = entryFromBuilder.getIssuerKeyEpoch();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuerKeyEpoch");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuerKeyEpoch");
    }

    {
        auto const& expected = auditorKeyEpochValue;

        auto const fromSleOpt = entryFromSle.getAuditorKeyEpoch();
        auto const fromBuilderOpt = entryFromBuilder.getAuditorKeyEpoch();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuditorKeyEpoch");
        expectEqualField(expected, *fromBuilderOpt, "sfAuditorKeyEpoch");
    }

    {
        auto const& expected = holderCountValue;

        auto const fromSleOpt = entryFromSle.getHolderCount();
        auto const fromBuilderOpt = entryFromBuilder.getHolderCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHolderCount");
        expectEqualField(expected, *fromBuilderOpt, "sfHolderCount");
    }

    {
        auto const& expected = confidentialOutstandingAmountValue;

        auto const fromSleOpt = entryFromSle.getConfidentialOutstandingAmount();
        auto const fromBuilderOpt = entryFromBuilder.getConfidentialOutstandingAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfConfidentialOutstandingAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfConfidentialOutstandingAmount");
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

    auto const issuerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const outstandingAmountValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    MPTokenIssuanceBuilder builder{
        issuerValue,
        sequenceValue,
        ownerNodeValue,
        outstandingAmountValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasTransferFee());
    EXPECT_FALSE(entry.getTransferFee().has_value());
    EXPECT_FALSE(entry.hasAssetScale());
    EXPECT_FALSE(entry.getAssetScale().has_value());
    EXPECT_FALSE(entry.hasMaximumAmount());
    EXPECT_FALSE(entry.getMaximumAmount().has_value());
    EXPECT_FALSE(entry.hasLockedAmount());
    EXPECT_FALSE(entry.getLockedAmount().has_value());
    EXPECT_FALSE(entry.hasMPTokenMetadata());
    EXPECT_FALSE(entry.getMPTokenMetadata().has_value());
    EXPECT_FALSE(entry.hasDomainID());
    EXPECT_FALSE(entry.getDomainID().has_value());
    EXPECT_FALSE(entry.hasImmutableFlags());
    EXPECT_FALSE(entry.getImmutableFlags().has_value());
    EXPECT_FALSE(entry.hasReferenceHolding());
    EXPECT_FALSE(entry.getReferenceHolding().has_value());
    EXPECT_FALSE(entry.hasIssuerEncryptionKey());
    EXPECT_FALSE(entry.getIssuerEncryptionKey().has_value());
    EXPECT_FALSE(entry.hasAuditorEncryptionKey());
    EXPECT_FALSE(entry.getAuditorEncryptionKey().has_value());
    EXPECT_FALSE(entry.hasIssuerKeyEpoch());
    EXPECT_FALSE(entry.getIssuerKeyEpoch().has_value());
    EXPECT_FALSE(entry.hasAuditorKeyEpoch());
    EXPECT_FALSE(entry.getAuditorKeyEpoch().has_value());
    EXPECT_FALSE(entry.hasHolderCount());
    EXPECT_FALSE(entry.getHolderCount().has_value());
    EXPECT_FALSE(entry.hasConfidentialOutstandingAmount());
    EXPECT_FALSE(entry.getConfidentialOutstandingAmount().has_value());
}
}
