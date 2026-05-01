// Auto-generated unit tests for ledger entry NFTokenOffer


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/NFTokenOffer.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

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
    };

    builder.setOwner(ownerValue);
    builder.setNFTokenID(nFTokenIDValue);
    builder.setAmount(amountValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setNFTokenOfferNode(nFTokenOfferNodeValue);
    builder.setDestination(destinationValue);
    builder.setExpiration(expirationValue);
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
        auto const& expected = nFTokenIDValue;
        auto const actualOpt = entry.getNFTokenID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfNFTokenID");
        EXPECT_TRUE(entry.hasNFTokenID());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = entry.getAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(entry.hasAmount());
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actualOpt = entry.getOwnerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerNode");
        EXPECT_TRUE(entry.hasOwnerNode());
    }

    {
        auto const& expected = nFTokenOfferNodeValue;
        auto const actualOpt = entry.getNFTokenOfferNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfNFTokenOfferNode");
        EXPECT_TRUE(entry.hasNFTokenOfferNode());
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = entry.getDestination();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(entry.hasDestination());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
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

    auto sle = std::make_shared<SLE>(NFTokenOffer::entryType, index);

    sle->at(sfOwner) = ownerValue;
    sle->at(sfNFTokenID) = nFTokenIDValue;
    sle->at(sfAmount) = amountValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfNFTokenOfferNode) = nFTokenOfferNodeValue;
    sle->at(sfDestination) = destinationValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    NFTokenOfferBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    NFTokenOffer entryFromSle{sle};
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
        auto const& expected = nFTokenIDValue;

        auto const fromSleOpt = entryFromSle.getNFTokenID();
        auto const fromBuilderOpt = entryFromBuilder.getNFTokenID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfNFTokenID");
        expectEqualField(expected, *fromBuilderOpt, "sfNFTokenID");
    }

    {
        auto const& expected = amountValue;

        auto const fromSleOpt = entryFromSle.getAmount();
        auto const fromBuilderOpt = entryFromBuilder.getAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfAmount");
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
        auto const& expected = nFTokenOfferNodeValue;

        auto const fromSleOpt = entryFromSle.getNFTokenOfferNode();
        auto const fromBuilderOpt = entryFromBuilder.getNFTokenOfferNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfNFTokenOfferNode");
        expectEqualField(expected, *fromBuilderOpt, "sfNFTokenOfferNode");
    }

    {
        auto const& expected = destinationValue;

        auto const fromSleOpt = entryFromSle.getDestination();
        auto const fromBuilderOpt = entryFromBuilder.getDestination();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDestination");
        expectEqualField(expected, *fromBuilderOpt, "sfDestination");
    }

    {
        auto const& expected = expirationValue;

        auto const fromSleOpt = entryFromSle.getExpiration();
        auto const fromBuilderOpt = entryFromBuilder.getExpiration();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExpiration");
        expectEqualField(expected, *fromBuilderOpt, "sfExpiration");
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
TEST(NFTokenOfferTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(NFTokenOffer{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(NFTokenOfferTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(NFTokenOfferBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(NFTokenOfferTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    NFTokenOfferBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasOwner());
    EXPECT_FALSE(entry.getOwner().has_value());
    EXPECT_FALSE(entry.hasNFTokenID());
    EXPECT_FALSE(entry.getNFTokenID().has_value());
    EXPECT_FALSE(entry.hasAmount());
    EXPECT_FALSE(entry.getAmount().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasNFTokenOfferNode());
    EXPECT_FALSE(entry.getNFTokenOfferNode().has_value());
    EXPECT_FALSE(entry.hasDestination());
    EXPECT_FALSE(entry.getDestination().has_value());
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
