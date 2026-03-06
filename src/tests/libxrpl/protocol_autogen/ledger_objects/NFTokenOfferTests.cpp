// Auto-generated unit tests for ledger entry NFTokenOffer

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/NFTokenOffer.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(NFTokenOfferTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const nFTokenIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const nFTokenOfferNodeValue = canonical_UINT64();
    auto const destinationValue = canonical_ACCOUNT();
    auto const expirationValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    NFTokenOfferBuilder builder{
        ownerValue,
        nFTokenIDValue,
        amountValue,
        ownerNodeValue,
        nFTokenOfferNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setDestination(destinationValue);
    builder.setExpiration(expirationValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry->validate());

    {
        auto const& expected = ownerValue;
        auto const actual = entry->getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = nFTokenIDValue;
        auto const actual = entry->getNFTokenID();
        expectEqualField(expected, actual, "sfNFTokenID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = entry->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry->getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = nFTokenOfferNodeValue;
        auto const actual = entry->getNFTokenOfferNode();
        expectEqualField(expected, actual, "sfNFTokenOfferNode");
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
        auto const& expected = destinationValue;
        auto const actualOpt = entry->getDestination();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(entry->hasDestination());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry->getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry->hasExpiration());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(NFTokenOfferTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const nFTokenIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const nFTokenOfferNodeValue = canonical_UINT64();
    auto const destinationValue = canonical_ACCOUNT();
    auto const expirationValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    SLE sle{NFTokenOffer::entryType, index};

    sle[sfOwner] = ownerValue;
    sle[sfNFTokenID] = nFTokenIDValue;
    sle[sfAmount] = amountValue;
    sle[sfOwnerNode] = ownerNodeValue;
    sle[sfNFTokenOfferNode] = nFTokenOfferNodeValue;
    sle[sfDestination] = destinationValue;
    sle[sfExpiration] = expirationValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;

    NFTokenOfferBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    NFTokenOffer entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder->validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder->getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = nFTokenIDValue;

        auto const fromSle = entryFromSle.getNFTokenID();
        auto const fromBuilder = entryFromBuilder->getNFTokenID();

        expectEqualField(expected, fromSle, "sfNFTokenID");
        expectEqualField(expected, fromBuilder, "sfNFTokenID");
    }

    {
        auto const& expected = amountValue;

        auto const fromSle = entryFromSle.getAmount();
        auto const fromBuilder = entryFromBuilder->getAmount();

        expectEqualField(expected, fromSle, "sfAmount");
        expectEqualField(expected, fromBuilder, "sfAmount");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder->getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = nFTokenOfferNodeValue;

        auto const fromSle = entryFromSle.getNFTokenOfferNode();
        auto const fromBuilder = entryFromBuilder->getNFTokenOfferNode();

        expectEqualField(expected, fromSle, "sfNFTokenOfferNode");
        expectEqualField(expected, fromBuilder, "sfNFTokenOfferNode");
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
        auto const& expected = destinationValue;

        auto const fromSleOpt = entryFromSle.getDestination();
        auto const fromBuilderOpt = entryFromBuilder->getDestination();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDestination");
        expectEqualField(expected, *fromBuilderOpt, "sfDestination");
    }

    {
        auto const& expected = expirationValue;

        auto const fromSleOpt = entryFromSle.getExpiration();
        auto const fromBuilderOpt = entryFromBuilder->getExpiration();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExpiration");
        expectEqualField(expected, *fromBuilderOpt, "sfExpiration");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
