// Auto-generated unit tests for ledger entry Repo


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Repo.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(RepoTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const collateralAmountValue = canonical_AMOUNT();
    auto const purchasePriceValue = canonical_AMOUNT();
    auto const interestRateValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const startDateValue = canonical_UINT32();
    auto const maturityDateValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const transferRateValue = canonical_UINT32();
    auto const dataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const destinationNodeValue = canonical_UINT64();
    auto const issuerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    RepoBuilder builder{
        accountValue,
        counterpartyValue,
        collateralAmountValue,
        purchasePriceValue,
        interestRateValue,
        expirationValue,
        maturityDateValue,
        gracePeriodValue,
        ownerNodeValue,
        destinationNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setStartDate(startDateValue);
    builder.setTransferRate(transferRateValue);
    builder.setData(dataValue);
    builder.setIssuerNode(issuerNodeValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = counterpartyValue;
        auto const actual = entry.getCounterparty();
        expectEqualField(expected, actual, "sfCounterparty");
    }

    {
        auto const& expected = collateralAmountValue;
        auto const actual = entry.getCollateralAmount();
        expectEqualField(expected, actual, "sfCollateralAmount");
    }

    {
        auto const& expected = purchasePriceValue;
        auto const actual = entry.getPurchasePrice();
        expectEqualField(expected, actual, "sfPurchasePrice");
    }

    {
        auto const& expected = interestRateValue;
        auto const actual = entry.getInterestRate();
        expectEqualField(expected, actual, "sfInterestRate");
    }

    {
        auto const& expected = expirationValue;
        auto const actual = entry.getExpiration();
        expectEqualField(expected, actual, "sfExpiration");
    }

    {
        auto const& expected = maturityDateValue;
        auto const actual = entry.getMaturityDate();
        expectEqualField(expected, actual, "sfMaturityDate");
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actual = entry.getGracePeriod();
        expectEqualField(expected, actual, "sfGracePeriod");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = destinationNodeValue;
        auto const actual = entry.getDestinationNode();
        expectEqualField(expected, actual, "sfDestinationNode");
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
        auto const& expected = startDateValue;
        auto const actualOpt = entry.getStartDate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfStartDate");
        EXPECT_TRUE(entry.hasStartDate());
    }

    {
        auto const& expected = transferRateValue;
        auto const actualOpt = entry.getTransferRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTransferRate");
        EXPECT_TRUE(entry.hasTransferRate());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = entry.getData();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(entry.hasData());
    }

    {
        auto const& expected = issuerNodeValue;
        auto const actualOpt = entry.getIssuerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuerNode");
        EXPECT_TRUE(entry.hasIssuerNode());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(RepoTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const collateralAmountValue = canonical_AMOUNT();
    auto const purchasePriceValue = canonical_AMOUNT();
    auto const interestRateValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const startDateValue = canonical_UINT32();
    auto const maturityDateValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const transferRateValue = canonical_UINT32();
    auto const dataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();
    auto const destinationNodeValue = canonical_UINT64();
    auto const issuerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(Repo::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfCounterparty) = counterpartyValue;
    sle->at(sfCollateralAmount) = collateralAmountValue;
    sle->at(sfPurchasePrice) = purchasePriceValue;
    sle->at(sfInterestRate) = interestRateValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfStartDate) = startDateValue;
    sle->at(sfMaturityDate) = maturityDateValue;
    sle->at(sfGracePeriod) = gracePeriodValue;
    sle->at(sfTransferRate) = transferRateValue;
    sle->at(sfData) = dataValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfDestinationNode) = destinationNodeValue;
    sle->at(sfIssuerNode) = issuerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    RepoBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Repo entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = counterpartyValue;

        auto const fromSle = entryFromSle.getCounterparty();
        auto const fromBuilder = entryFromBuilder.getCounterparty();

        expectEqualField(expected, fromSle, "sfCounterparty");
        expectEqualField(expected, fromBuilder, "sfCounterparty");
    }

    {
        auto const& expected = collateralAmountValue;

        auto const fromSle = entryFromSle.getCollateralAmount();
        auto const fromBuilder = entryFromBuilder.getCollateralAmount();

        expectEqualField(expected, fromSle, "sfCollateralAmount");
        expectEqualField(expected, fromBuilder, "sfCollateralAmount");
    }

    {
        auto const& expected = purchasePriceValue;

        auto const fromSle = entryFromSle.getPurchasePrice();
        auto const fromBuilder = entryFromBuilder.getPurchasePrice();

        expectEqualField(expected, fromSle, "sfPurchasePrice");
        expectEqualField(expected, fromBuilder, "sfPurchasePrice");
    }

    {
        auto const& expected = interestRateValue;

        auto const fromSle = entryFromSle.getInterestRate();
        auto const fromBuilder = entryFromBuilder.getInterestRate();

        expectEqualField(expected, fromSle, "sfInterestRate");
        expectEqualField(expected, fromBuilder, "sfInterestRate");
    }

    {
        auto const& expected = expirationValue;

        auto const fromSle = entryFromSle.getExpiration();
        auto const fromBuilder = entryFromBuilder.getExpiration();

        expectEqualField(expected, fromSle, "sfExpiration");
        expectEqualField(expected, fromBuilder, "sfExpiration");
    }

    {
        auto const& expected = maturityDateValue;

        auto const fromSle = entryFromSle.getMaturityDate();
        auto const fromBuilder = entryFromBuilder.getMaturityDate();

        expectEqualField(expected, fromSle, "sfMaturityDate");
        expectEqualField(expected, fromBuilder, "sfMaturityDate");
    }

    {
        auto const& expected = gracePeriodValue;

        auto const fromSle = entryFromSle.getGracePeriod();
        auto const fromBuilder = entryFromBuilder.getGracePeriod();

        expectEqualField(expected, fromSle, "sfGracePeriod");
        expectEqualField(expected, fromBuilder, "sfGracePeriod");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = destinationNodeValue;

        auto const fromSle = entryFromSle.getDestinationNode();
        auto const fromBuilder = entryFromBuilder.getDestinationNode();

        expectEqualField(expected, fromSle, "sfDestinationNode");
        expectEqualField(expected, fromBuilder, "sfDestinationNode");
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
        auto const& expected = startDateValue;

        auto const fromSleOpt = entryFromSle.getStartDate();
        auto const fromBuilderOpt = entryFromBuilder.getStartDate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfStartDate");
        expectEqualField(expected, *fromBuilderOpt, "sfStartDate");
    }

    {
        auto const& expected = transferRateValue;

        auto const fromSleOpt = entryFromSle.getTransferRate();
        auto const fromBuilderOpt = entryFromBuilder.getTransferRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTransferRate");
        expectEqualField(expected, *fromBuilderOpt, "sfTransferRate");
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
        auto const& expected = issuerNodeValue;

        auto const fromSleOpt = entryFromSle.getIssuerNode();
        auto const fromBuilderOpt = entryFromBuilder.getIssuerNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuerNode");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuerNode");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(RepoTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Repo{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(RepoTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(RepoBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(RepoTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const accountValue = canonical_ACCOUNT();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const collateralAmountValue = canonical_AMOUNT();
    auto const purchasePriceValue = canonical_AMOUNT();
    auto const interestRateValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const maturityDateValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const destinationNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    RepoBuilder builder{
        accountValue,
        counterpartyValue,
        collateralAmountValue,
        purchasePriceValue,
        interestRateValue,
        expirationValue,
        maturityDateValue,
        gracePeriodValue,
        ownerNodeValue,
        destinationNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasStartDate());
    EXPECT_FALSE(entry.getStartDate().has_value());
    EXPECT_FALSE(entry.hasTransferRate());
    EXPECT_FALSE(entry.getTransferRate().has_value());
    EXPECT_FALSE(entry.hasData());
    EXPECT_FALSE(entry.getData().has_value());
    EXPECT_FALSE(entry.hasIssuerNode());
    EXPECT_FALSE(entry.getIssuerNode().has_value());
}
}
