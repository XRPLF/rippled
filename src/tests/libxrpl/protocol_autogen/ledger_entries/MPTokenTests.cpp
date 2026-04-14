// Auto-generated unit tests for ledger entry MPToken


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/MPToken.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(MPTokenTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const lockedAmountValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const confidentialBalanceInboxValue = canonical_VL();
    auto const confidentialBalanceSpendingValue = canonical_VL();
    auto const confidentialBalanceVersionValue = canonical_UINT32();
    auto const issuerEncryptedBalanceValue = canonical_VL();
    auto const auditorEncryptedBalanceValue = canonical_VL();
    auto const holderEncryptionKeyValue = canonical_VL();

    MPTokenBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setMPTAmount(mPTAmountValue);
    builder.setLockedAmount(lockedAmountValue);
    builder.setConfidentialBalanceInbox(confidentialBalanceInboxValue);
    builder.setConfidentialBalanceSpending(confidentialBalanceSpendingValue);
    builder.setConfidentialBalanceVersion(confidentialBalanceVersionValue);
    builder.setIssuerEncryptedBalance(issuerEncryptedBalanceValue);
    builder.setAuditorEncryptedBalance(auditorEncryptedBalanceValue);
    builder.setHolderEncryptionKey(holderEncryptionKeyValue);

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
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = entry.getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
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
        auto const& expected = mPTAmountValue;
        auto const actualOpt = entry.getMPTAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMPTAmount");
        EXPECT_TRUE(entry.hasMPTAmount());
    }

    {
        auto const& expected = lockedAmountValue;
        auto const actualOpt = entry.getLockedAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLockedAmount");
        EXPECT_TRUE(entry.hasLockedAmount());
    }

    {
        auto const& expected = confidentialBalanceInboxValue;
        auto const actualOpt = entry.getConfidentialBalanceInbox();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceInbox");
        EXPECT_TRUE(entry.hasConfidentialBalanceInbox());
    }

    {
        auto const& expected = confidentialBalanceSpendingValue;
        auto const actualOpt = entry.getConfidentialBalanceSpending();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceSpending");
        EXPECT_TRUE(entry.hasConfidentialBalanceSpending());
    }

    {
        auto const& expected = confidentialBalanceVersionValue;
        auto const actualOpt = entry.getConfidentialBalanceVersion();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceVersion");
        EXPECT_TRUE(entry.hasConfidentialBalanceVersion());
    }

    {
        auto const& expected = issuerEncryptedBalanceValue;
        auto const actualOpt = entry.getIssuerEncryptedBalance();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuerEncryptedBalance");
        EXPECT_TRUE(entry.hasIssuerEncryptedBalance());
    }

    {
        auto const& expected = auditorEncryptedBalanceValue;
        auto const actualOpt = entry.getAuditorEncryptedBalance();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedBalance");
        EXPECT_TRUE(entry.hasAuditorEncryptedBalance());
    }

    {
        auto const& expected = holderEncryptionKeyValue;
        auto const actualOpt = entry.getHolderEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHolderEncryptionKey");
        EXPECT_TRUE(entry.hasHolderEncryptionKey());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(MPTokenTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const lockedAmountValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const confidentialBalanceInboxValue = canonical_VL();
    auto const confidentialBalanceSpendingValue = canonical_VL();
    auto const confidentialBalanceVersionValue = canonical_UINT32();
    auto const issuerEncryptedBalanceValue = canonical_VL();
    auto const auditorEncryptedBalanceValue = canonical_VL();
    auto const holderEncryptionKeyValue = canonical_VL();

    auto sle = std::make_shared<SLE>(MPToken::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfMPTokenIssuanceID) = mPTokenIssuanceIDValue;
    sle->at(sfMPTAmount) = mPTAmountValue;
    sle->at(sfLockedAmount) = lockedAmountValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfConfidentialBalanceInbox) = confidentialBalanceInboxValue;
    sle->at(sfConfidentialBalanceSpending) = confidentialBalanceSpendingValue;
    sle->at(sfConfidentialBalanceVersion) = confidentialBalanceVersionValue;
    sle->at(sfIssuerEncryptedBalance) = issuerEncryptedBalanceValue;
    sle->at(sfAuditorEncryptedBalance) = auditorEncryptedBalanceValue;
    sle->at(sfHolderEncryptionKey) = holderEncryptionKeyValue;

    MPTokenBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    MPToken entryFromSle{sle};
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
        auto const& expected = mPTokenIssuanceIDValue;

        auto const fromSle = entryFromSle.getMPTokenIssuanceID();
        auto const fromBuilder = entryFromBuilder.getMPTokenIssuanceID();

        expectEqualField(expected, fromSle, "sfMPTokenIssuanceID");
        expectEqualField(expected, fromBuilder, "sfMPTokenIssuanceID");
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
        auto const& expected = mPTAmountValue;

        auto const fromSleOpt = entryFromSle.getMPTAmount();
        auto const fromBuilderOpt = entryFromBuilder.getMPTAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMPTAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfMPTAmount");
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
        auto const& expected = confidentialBalanceInboxValue;

        auto const fromSleOpt = entryFromSle.getConfidentialBalanceInbox();
        auto const fromBuilderOpt = entryFromBuilder.getConfidentialBalanceInbox();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfConfidentialBalanceInbox");
        expectEqualField(expected, *fromBuilderOpt, "sfConfidentialBalanceInbox");
    }

    {
        auto const& expected = confidentialBalanceSpendingValue;

        auto const fromSleOpt = entryFromSle.getConfidentialBalanceSpending();
        auto const fromBuilderOpt = entryFromBuilder.getConfidentialBalanceSpending();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfConfidentialBalanceSpending");
        expectEqualField(expected, *fromBuilderOpt, "sfConfidentialBalanceSpending");
    }

    {
        auto const& expected = confidentialBalanceVersionValue;

        auto const fromSleOpt = entryFromSle.getConfidentialBalanceVersion();
        auto const fromBuilderOpt = entryFromBuilder.getConfidentialBalanceVersion();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfConfidentialBalanceVersion");
        expectEqualField(expected, *fromBuilderOpt, "sfConfidentialBalanceVersion");
    }

    {
        auto const& expected = issuerEncryptedBalanceValue;

        auto const fromSleOpt = entryFromSle.getIssuerEncryptedBalance();
        auto const fromBuilderOpt = entryFromBuilder.getIssuerEncryptedBalance();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuerEncryptedBalance");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuerEncryptedBalance");
    }

    {
        auto const& expected = auditorEncryptedBalanceValue;

        auto const fromSleOpt = entryFromSle.getAuditorEncryptedBalance();
        auto const fromBuilderOpt = entryFromBuilder.getAuditorEncryptedBalance();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuditorEncryptedBalance");
        expectEqualField(expected, *fromBuilderOpt, "sfAuditorEncryptedBalance");
    }

    {
        auto const& expected = holderEncryptionKeyValue;

        auto const fromSleOpt = entryFromSle.getHolderEncryptionKey();
        auto const fromBuilderOpt = entryFromBuilder.getHolderEncryptionKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHolderEncryptionKey");
        expectEqualField(expected, *fromBuilderOpt, "sfHolderEncryptionKey");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(MPTokenTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(MPToken{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(MPTokenTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(MPTokenBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(MPTokenTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const accountValue = canonical_ACCOUNT();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    MPTokenBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasMPTAmount());
    EXPECT_FALSE(entry.getMPTAmount().has_value());
    EXPECT_FALSE(entry.hasLockedAmount());
    EXPECT_FALSE(entry.getLockedAmount().has_value());
    EXPECT_FALSE(entry.hasConfidentialBalanceInbox());
    EXPECT_FALSE(entry.getConfidentialBalanceInbox().has_value());
    EXPECT_FALSE(entry.hasConfidentialBalanceSpending());
    EXPECT_FALSE(entry.getConfidentialBalanceSpending().has_value());
    EXPECT_FALSE(entry.hasConfidentialBalanceVersion());
    EXPECT_FALSE(entry.getConfidentialBalanceVersion().has_value());
    EXPECT_FALSE(entry.hasIssuerEncryptedBalance());
    EXPECT_FALSE(entry.getIssuerEncryptedBalance().has_value());
    EXPECT_FALSE(entry.hasAuditorEncryptedBalance());
    EXPECT_FALSE(entry.getAuditorEncryptedBalance().has_value());
    EXPECT_FALSE(entry.hasHolderEncryptionKey());
    EXPECT_FALSE(entry.getHolderEncryptionKey().has_value());
}
}
