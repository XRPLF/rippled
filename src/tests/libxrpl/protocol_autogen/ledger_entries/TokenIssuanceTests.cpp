// Auto-generated unit tests for ledger entry TokenIssuance


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/TokenIssuance.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(TokenIssuanceTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const issuerValue = canonical_ACCOUNT();
    auto const currencyValue = canonical_CURRENCY();
    auto const maximumAmountValue = canonical_UINT64();
    auto const issuedAmountValue = canonical_NUMBER();
    auto const tokenScaleValue = canonical_UINT8();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const transferFeeValue = canonical_UINT16();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    TokenIssuanceBuilder builder{
        issuerValue,
        currencyValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setMaximumAmount(maximumAmountValue);
    builder.setIssuedAmount(issuedAmountValue);
    builder.setTokenScale(tokenScaleValue);
    builder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    builder.setTransferFee(transferFeeValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);

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
        auto const& expected = currencyValue;
        auto const actual = entry.getCurrency();
        expectEqualField(expected, actual, "sfCurrency");
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
        auto const& expected = maximumAmountValue;
        auto const actualOpt = entry.getMaximumAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMaximumAmount");
        EXPECT_TRUE(entry.hasMaximumAmount());
    }

    {
        auto const& expected = issuedAmountValue;
        auto const actualOpt = entry.getIssuedAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuedAmount");
        EXPECT_TRUE(entry.hasIssuedAmount());
    }

    {
        auto const& expected = tokenScaleValue;
        auto const actualOpt = entry.getTokenScale();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTokenScale");
        EXPECT_TRUE(entry.hasTokenScale());
    }

    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = entry.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
        EXPECT_TRUE(entry.hasMPTokenIssuanceID());
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = entry.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(entry.hasTransferFee());
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = entry.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
        EXPECT_TRUE(entry.hasMPTokenMetadata());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(TokenIssuanceTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const issuerValue = canonical_ACCOUNT();
    auto const currencyValue = canonical_CURRENCY();
    auto const maximumAmountValue = canonical_UINT64();
    auto const issuedAmountValue = canonical_NUMBER();
    auto const tokenScaleValue = canonical_UINT8();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const transferFeeValue = canonical_UINT16();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(TokenIssuance::entryType, index);

    sle->at(sfIssuer) = issuerValue;
    sle->at(sfCurrency) = currencyValue;
    sle->at(sfMaximumAmount) = maximumAmountValue;
    sle->at(sfIssuedAmount) = issuedAmountValue;
    sle->at(sfTokenScale) = tokenScaleValue;
    sle->at(sfMPTokenIssuanceID) = mPTokenIssuanceIDValue;
    sle->at(sfTransferFee) = transferFeeValue;
    sle->at(sfMPTokenMetadata) = mPTokenMetadataValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    TokenIssuanceBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    TokenIssuance entryFromSle{sle};
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
        auto const& expected = currencyValue;

        auto const fromSle = entryFromSle.getCurrency();
        auto const fromBuilder = entryFromBuilder.getCurrency();

        expectEqualField(expected, fromSle, "sfCurrency");
        expectEqualField(expected, fromBuilder, "sfCurrency");
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
        auto const& expected = maximumAmountValue;

        auto const fromSleOpt = entryFromSle.getMaximumAmount();
        auto const fromBuilderOpt = entryFromBuilder.getMaximumAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMaximumAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfMaximumAmount");
    }

    {
        auto const& expected = issuedAmountValue;

        auto const fromSleOpt = entryFromSle.getIssuedAmount();
        auto const fromBuilderOpt = entryFromBuilder.getIssuedAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuedAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuedAmount");
    }

    {
        auto const& expected = tokenScaleValue;

        auto const fromSleOpt = entryFromSle.getTokenScale();
        auto const fromBuilderOpt = entryFromBuilder.getTokenScale();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTokenScale");
        expectEqualField(expected, *fromBuilderOpt, "sfTokenScale");
    }

    {
        auto const& expected = mPTokenIssuanceIDValue;

        auto const fromSleOpt = entryFromSle.getMPTokenIssuanceID();
        auto const fromBuilderOpt = entryFromBuilder.getMPTokenIssuanceID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMPTokenIssuanceID");
        expectEqualField(expected, *fromBuilderOpt, "sfMPTokenIssuanceID");
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
        auto const& expected = mPTokenMetadataValue;

        auto const fromSleOpt = entryFromSle.getMPTokenMetadata();
        auto const fromBuilderOpt = entryFromBuilder.getMPTokenMetadata();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMPTokenMetadata");
        expectEqualField(expected, *fromBuilderOpt, "sfMPTokenMetadata");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(TokenIssuanceTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(TokenIssuance{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(TokenIssuanceTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(TokenIssuanceBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TokenIssuanceTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const issuerValue = canonical_ACCOUNT();
    auto const currencyValue = canonical_CURRENCY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    TokenIssuanceBuilder builder{
        issuerValue,
        currencyValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasMaximumAmount());
    EXPECT_FALSE(entry.getMaximumAmount().has_value());
    EXPECT_FALSE(entry.hasIssuedAmount());
    EXPECT_FALSE(entry.getIssuedAmount().has_value());
    EXPECT_FALSE(entry.hasTokenScale());
    EXPECT_FALSE(entry.getTokenScale().has_value());
    EXPECT_FALSE(entry.hasMPTokenIssuanceID());
    EXPECT_FALSE(entry.getMPTokenIssuanceID().has_value());
    EXPECT_FALSE(entry.hasTransferFee());
    EXPECT_FALSE(entry.getTransferFee().has_value());
    EXPECT_FALSE(entry.hasMPTokenMetadata());
    EXPECT_FALSE(entry.getMPTokenMetadata().has_value());
}
}
