// Auto-generated unit tests for ledger entry VaultPosition


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/VaultPosition.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(VaultPositionTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const assetValue = canonical_ISSUE();
    auto const markPriceValue = canonical_NUMBER();
    auto const lastMarkTimeValue = canonical_UINT32();
    auto const costBasisValue = canonical_NUMBER();

    VaultPositionBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        vaultIDValue,
        assetValue,
        markPriceValue,
        lastMarkTimeValue
    };

    builder.setCostBasis(costBasisValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

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
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = vaultIDValue;
        auto const actual = entry.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
    }

    {
        auto const& expected = assetValue;
        auto const actual = entry.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = markPriceValue;
        auto const actual = entry.getMarkPrice();
        expectEqualField(expected, actual, "sfMarkPrice");
    }

    {
        auto const& expected = lastMarkTimeValue;
        auto const actual = entry.getLastMarkTime();
        expectEqualField(expected, actual, "sfLastMarkTime");
    }

    {
        auto const& expected = costBasisValue;
        auto const actualOpt = entry.getCostBasis();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCostBasis");
        EXPECT_TRUE(entry.hasCostBasis());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(VaultPositionTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const assetValue = canonical_ISSUE();
    auto const markPriceValue = canonical_NUMBER();
    auto const lastMarkTimeValue = canonical_UINT32();
    auto const costBasisValue = canonical_NUMBER();

    auto sle = std::make_shared<SLE>(VaultPosition::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfVaultID) = vaultIDValue;
    sle->at(sfAsset) = STIssue(sfAsset, assetValue);
    sle->at(sfMarkPrice) = markPriceValue;
    sle->at(sfLastMarkTime) = lastMarkTimeValue;
    sle->at(sfCostBasis) = costBasisValue;

    VaultPositionBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    VaultPosition entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

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
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = vaultIDValue;

        auto const fromSle = entryFromSle.getVaultID();
        auto const fromBuilder = entryFromBuilder.getVaultID();

        expectEqualField(expected, fromSle, "sfVaultID");
        expectEqualField(expected, fromBuilder, "sfVaultID");
    }

    {
        auto const& expected = assetValue;

        auto const fromSle = entryFromSle.getAsset();
        auto const fromBuilder = entryFromBuilder.getAsset();

        expectEqualField(expected, fromSle, "sfAsset");
        expectEqualField(expected, fromBuilder, "sfAsset");
    }

    {
        auto const& expected = markPriceValue;

        auto const fromSle = entryFromSle.getMarkPrice();
        auto const fromBuilder = entryFromBuilder.getMarkPrice();

        expectEqualField(expected, fromSle, "sfMarkPrice");
        expectEqualField(expected, fromBuilder, "sfMarkPrice");
    }

    {
        auto const& expected = lastMarkTimeValue;

        auto const fromSle = entryFromSle.getLastMarkTime();
        auto const fromBuilder = entryFromBuilder.getLastMarkTime();

        expectEqualField(expected, fromSle, "sfLastMarkTime");
        expectEqualField(expected, fromBuilder, "sfLastMarkTime");
    }

    {
        auto const& expected = costBasisValue;

        auto const fromSleOpt = entryFromSle.getCostBasis();
        auto const fromBuilderOpt = entryFromBuilder.getCostBasis();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCostBasis");
        expectEqualField(expected, *fromBuilderOpt, "sfCostBasis");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(VaultPositionTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(VaultPosition{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(VaultPositionTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(VaultPositionBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(VaultPositionTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const assetValue = canonical_ISSUE();
    auto const markPriceValue = canonical_NUMBER();
    auto const lastMarkTimeValue = canonical_UINT32();

    VaultPositionBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        vaultIDValue,
        assetValue,
        markPriceValue,
        lastMarkTimeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasCostBasis());
    EXPECT_FALSE(entry.getCostBasis().has_value());
}
}
