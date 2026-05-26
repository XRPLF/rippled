// Auto-generated unit tests for ledger entry Vault


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Vault.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(VaultTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const dataValue = canonical_VL();
    auto const assetValue = canonical_ISSUE();
    auto const assetsTotalValue = canonical_NUMBER();
    auto const assetsAvailableValue = canonical_NUMBER();
    auto const assetsMaximumValue = canonical_NUMBER();
    auto const lossUnrealizedValue = canonical_NUMBER();
    auto const shareMPTIDValue = canonical_UINT192();
    auto const withdrawalPolicyValue = canonical_UINT8();
    auto const scaleValue = canonical_UINT8();

    VaultBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        ownerValue,
        accountValue,
        assetValue,
        shareMPTIDValue,
        withdrawalPolicyValue
    };

    builder.setData(dataValue);
    builder.setAssetsTotal(assetsTotalValue);
    builder.setAssetsAvailable(assetsAvailableValue);
    builder.setAssetsMaximum(assetsMaximumValue);
    builder.setLossUnrealized(lossUnrealizedValue);
    builder.setScale(scaleValue);

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
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = assetValue;
        auto const actual = entry.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = shareMPTIDValue;
        auto const actual = entry.getShareMPTID();
        expectEqualField(expected, actual, "sfShareMPTID");
    }

    {
        auto const& expected = withdrawalPolicyValue;
        auto const actual = entry.getWithdrawalPolicy();
        expectEqualField(expected, actual, "sfWithdrawalPolicy");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = entry.getData();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(entry.hasData());
    }

    {
        auto const& expected = assetsTotalValue;
        auto const actualOpt = entry.getAssetsTotal();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAssetsTotal");
        EXPECT_TRUE(entry.hasAssetsTotal());
    }

    {
        auto const& expected = assetsAvailableValue;
        auto const actualOpt = entry.getAssetsAvailable();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAssetsAvailable");
        EXPECT_TRUE(entry.hasAssetsAvailable());
    }

    {
        auto const& expected = assetsMaximumValue;
        auto const actualOpt = entry.getAssetsMaximum();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAssetsMaximum");
        EXPECT_TRUE(entry.hasAssetsMaximum());
    }

    {
        auto const& expected = lossUnrealizedValue;
        auto const actualOpt = entry.getLossUnrealized();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLossUnrealized");
        EXPECT_TRUE(entry.hasLossUnrealized());
    }

    {
        auto const& expected = scaleValue;
        auto const actualOpt = entry.getScale();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfScale");
        EXPECT_TRUE(entry.hasScale());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(VaultTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const dataValue = canonical_VL();
    auto const assetValue = canonical_ISSUE();
    auto const assetsTotalValue = canonical_NUMBER();
    auto const assetsAvailableValue = canonical_NUMBER();
    auto const assetsMaximumValue = canonical_NUMBER();
    auto const lossUnrealizedValue = canonical_NUMBER();
    auto const shareMPTIDValue = canonical_UINT192();
    auto const withdrawalPolicyValue = canonical_UINT8();
    auto const scaleValue = canonical_UINT8();

    auto sle = std::make_shared<SLE>(Vault::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfOwner) = ownerValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfData) = dataValue;
    sle->at(sfAsset) = STIssue(sfAsset, assetValue);
    sle->at(sfAssetsTotal) = assetsTotalValue;
    sle->at(sfAssetsAvailable) = assetsAvailableValue;
    sle->at(sfAssetsMaximum) = assetsMaximumValue;
    sle->at(sfLossUnrealized) = lossUnrealizedValue;
    sle->at(sfShareMPTID) = shareMPTIDValue;
    sle->at(sfWithdrawalPolicy) = withdrawalPolicyValue;
    sle->at(sfScale) = scaleValue;

    VaultBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Vault entryFromSle{sle};
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
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = assetValue;

        auto const fromSle = entryFromSle.getAsset();
        auto const fromBuilder = entryFromBuilder.getAsset();

        expectEqualField(expected, fromSle, "sfAsset");
        expectEqualField(expected, fromBuilder, "sfAsset");
    }

    {
        auto const& expected = shareMPTIDValue;

        auto const fromSle = entryFromSle.getShareMPTID();
        auto const fromBuilder = entryFromBuilder.getShareMPTID();

        expectEqualField(expected, fromSle, "sfShareMPTID");
        expectEqualField(expected, fromBuilder, "sfShareMPTID");
    }

    {
        auto const& expected = withdrawalPolicyValue;

        auto const fromSle = entryFromSle.getWithdrawalPolicy();
        auto const fromBuilder = entryFromBuilder.getWithdrawalPolicy();

        expectEqualField(expected, fromSle, "sfWithdrawalPolicy");
        expectEqualField(expected, fromBuilder, "sfWithdrawalPolicy");
    }

    {
        auto const& expected = dataValue;

        auto const fromSleOpt = entryFromSle.getData();
        auto const fromBuilderOpt = entryFromBuilder.getData();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfData");
        expectEqualField(expected, *fromBuilderOpt, "sfData");
    }

    {
        auto const& expected = assetsTotalValue;

        auto const fromSleOpt = entryFromSle.getAssetsTotal();
        auto const fromBuilderOpt = entryFromBuilder.getAssetsTotal();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAssetsTotal");
        expectEqualField(expected, *fromBuilderOpt, "sfAssetsTotal");
    }

    {
        auto const& expected = assetsAvailableValue;

        auto const fromSleOpt = entryFromSle.getAssetsAvailable();
        auto const fromBuilderOpt = entryFromBuilder.getAssetsAvailable();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAssetsAvailable");
        expectEqualField(expected, *fromBuilderOpt, "sfAssetsAvailable");
    }

    {
        auto const& expected = assetsMaximumValue;

        auto const fromSleOpt = entryFromSle.getAssetsMaximum();
        auto const fromBuilderOpt = entryFromBuilder.getAssetsMaximum();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAssetsMaximum");
        expectEqualField(expected, *fromBuilderOpt, "sfAssetsMaximum");
    }

    {
        auto const& expected = lossUnrealizedValue;

        auto const fromSleOpt = entryFromSle.getLossUnrealized();
        auto const fromBuilderOpt = entryFromBuilder.getLossUnrealized();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLossUnrealized");
        expectEqualField(expected, *fromBuilderOpt, "sfLossUnrealized");
    }

    {
        auto const& expected = scaleValue;

        auto const fromSleOpt = entryFromSle.getScale();
        auto const fromBuilderOpt = entryFromBuilder.getScale();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfScale");
        expectEqualField(expected, *fromBuilderOpt, "sfScale");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(VaultTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Vault{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(VaultTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(VaultBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(VaultTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const shareMPTIDValue = canonical_UINT192();
    auto const withdrawalPolicyValue = canonical_UINT8();

    VaultBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        ownerValue,
        accountValue,
        assetValue,
        shareMPTIDValue,
        withdrawalPolicyValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasData());
    EXPECT_FALSE(entry.getData().has_value());
    EXPECT_FALSE(entry.hasAssetsTotal());
    EXPECT_FALSE(entry.getAssetsTotal().has_value());
    EXPECT_FALSE(entry.hasAssetsAvailable());
    EXPECT_FALSE(entry.getAssetsAvailable().has_value());
    EXPECT_FALSE(entry.hasAssetsMaximum());
    EXPECT_FALSE(entry.getAssetsMaximum().has_value());
    EXPECT_FALSE(entry.hasLossUnrealized());
    EXPECT_FALSE(entry.getLossUnrealized().has_value());
    EXPECT_FALSE(entry.hasScale());
    EXPECT_FALSE(entry.getScale().has_value());
}
}
