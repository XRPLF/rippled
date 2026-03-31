// Auto-generated unit tests for ledger entry AccountRoot


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AccountRoot.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AccountRootTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const balanceValue = canonical_AMOUNT();
    auto const ownerCountValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountTxnIDValue = canonical_UINT256();
    auto const regularKeyValue = canonical_ACCOUNT();
    auto const emailHashValue = canonical_UINT128();
    auto const walletLocatorValue = canonical_UINT256();
    auto const walletSizeValue = canonical_UINT32();
    auto const messageKeyValue = canonical_VL();
    auto const transferRateValue = canonical_UINT32();
    auto const domainValue = canonical_VL();
    auto const tickSizeValue = canonical_UINT8();
    auto const ticketCountValue = canonical_UINT32();
    auto const nFTokenMinterValue = canonical_ACCOUNT();
    auto const mintedNFTokensValue = canonical_UINT32();
    auto const burnedNFTokensValue = canonical_UINT32();
    auto const firstNFTokenSequenceValue = canonical_UINT32();
    auto const aMMIDValue = canonical_UINT256();
    auto const vaultIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();

    AccountRootBuilder builder{
        accountValue,
        sequenceValue,
        balanceValue,
        ownerCountValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setAccountTxnID(accountTxnIDValue);
    builder.setRegularKey(regularKeyValue);
    builder.setEmailHash(emailHashValue);
    builder.setWalletLocator(walletLocatorValue);
    builder.setWalletSize(walletSizeValue);
    builder.setMessageKey(messageKeyValue);
    builder.setTransferRate(transferRateValue);
    builder.setDomain(domainValue);
    builder.setTickSize(tickSizeValue);
    builder.setTicketCount(ticketCountValue);
    builder.setNFTokenMinter(nFTokenMinterValue);
    builder.setMintedNFTokens(mintedNFTokensValue);
    builder.setBurnedNFTokens(burnedNFTokensValue);
    builder.setFirstNFTokenSequence(firstNFTokenSequenceValue);
    builder.setAMMID(aMMIDValue);
    builder.setVaultID(vaultIDValue);
    builder.setLoanBrokerID(loanBrokerIDValue);

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
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = balanceValue;
        auto const actual = entry.getBalance();
        expectEqualField(expected, actual, "sfBalance");
    }

    {
        auto const& expected = ownerCountValue;
        auto const actual = entry.getOwnerCount();
        expectEqualField(expected, actual, "sfOwnerCount");
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
        auto const& expected = accountTxnIDValue;
        auto const actualOpt = entry.getAccountTxnID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAccountTxnID");
        EXPECT_TRUE(entry.hasAccountTxnID());
    }

    {
        auto const& expected = regularKeyValue;
        auto const actualOpt = entry.getRegularKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfRegularKey");
        EXPECT_TRUE(entry.hasRegularKey());
    }

    {
        auto const& expected = emailHashValue;
        auto const actualOpt = entry.getEmailHash();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfEmailHash");
        EXPECT_TRUE(entry.hasEmailHash());
    }

    {
        auto const& expected = walletLocatorValue;
        auto const actualOpt = entry.getWalletLocator();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfWalletLocator");
        EXPECT_TRUE(entry.hasWalletLocator());
    }

    {
        auto const& expected = walletSizeValue;
        auto const actualOpt = entry.getWalletSize();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfWalletSize");
        EXPECT_TRUE(entry.hasWalletSize());
    }

    {
        auto const& expected = messageKeyValue;
        auto const actualOpt = entry.getMessageKey();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMessageKey");
        EXPECT_TRUE(entry.hasMessageKey());
    }

    {
        auto const& expected = transferRateValue;
        auto const actualOpt = entry.getTransferRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTransferRate");
        EXPECT_TRUE(entry.hasTransferRate());
    }

    {
        auto const& expected = domainValue;
        auto const actualOpt = entry.getDomain();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomain");
        EXPECT_TRUE(entry.hasDomain());
    }

    {
        auto const& expected = tickSizeValue;
        auto const actualOpt = entry.getTickSize();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTickSize");
        EXPECT_TRUE(entry.hasTickSize());
    }

    {
        auto const& expected = ticketCountValue;
        auto const actualOpt = entry.getTicketCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTicketCount");
        EXPECT_TRUE(entry.hasTicketCount());
    }

    {
        auto const& expected = nFTokenMinterValue;
        auto const actualOpt = entry.getNFTokenMinter();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfNFTokenMinter");
        EXPECT_TRUE(entry.hasNFTokenMinter());
    }

    {
        auto const& expected = mintedNFTokensValue;
        auto const actualOpt = entry.getMintedNFTokens();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMintedNFTokens");
        EXPECT_TRUE(entry.hasMintedNFTokens());
    }

    {
        auto const& expected = burnedNFTokensValue;
        auto const actualOpt = entry.getBurnedNFTokens();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBurnedNFTokens");
        EXPECT_TRUE(entry.hasBurnedNFTokens());
    }

    {
        auto const& expected = firstNFTokenSequenceValue;
        auto const actualOpt = entry.getFirstNFTokenSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFirstNFTokenSequence");
        EXPECT_TRUE(entry.hasFirstNFTokenSequence());
    }

    {
        auto const& expected = aMMIDValue;
        auto const actualOpt = entry.getAMMID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAMMID");
        EXPECT_TRUE(entry.hasAMMID());
    }

    {
        auto const& expected = vaultIDValue;
        auto const actualOpt = entry.getVaultID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultID");
        EXPECT_TRUE(entry.hasVaultID());
    }

    {
        auto const& expected = loanBrokerIDValue;
        auto const actualOpt = entry.getLoanBrokerID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLoanBrokerID");
        EXPECT_TRUE(entry.hasLoanBrokerID());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(AccountRootTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const balanceValue = canonical_AMOUNT();
    auto const ownerCountValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountTxnIDValue = canonical_UINT256();
    auto const regularKeyValue = canonical_ACCOUNT();
    auto const emailHashValue = canonical_UINT128();
    auto const walletLocatorValue = canonical_UINT256();
    auto const walletSizeValue = canonical_UINT32();
    auto const messageKeyValue = canonical_VL();
    auto const transferRateValue = canonical_UINT32();
    auto const domainValue = canonical_VL();
    auto const tickSizeValue = canonical_UINT8();
    auto const ticketCountValue = canonical_UINT32();
    auto const nFTokenMinterValue = canonical_ACCOUNT();
    auto const mintedNFTokensValue = canonical_UINT32();
    auto const burnedNFTokensValue = canonical_UINT32();
    auto const firstNFTokenSequenceValue = canonical_UINT32();
    auto const aMMIDValue = canonical_UINT256();
    auto const vaultIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();

    auto sle = std::make_shared<SLE>(AccountRoot::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfBalance) = balanceValue;
    sle->at(sfOwnerCount) = ownerCountValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfAccountTxnID) = accountTxnIDValue;
    sle->at(sfRegularKey) = regularKeyValue;
    sle->at(sfEmailHash) = emailHashValue;
    sle->at(sfWalletLocator) = walletLocatorValue;
    sle->at(sfWalletSize) = walletSizeValue;
    sle->at(sfMessageKey) = messageKeyValue;
    sle->at(sfTransferRate) = transferRateValue;
    sle->at(sfDomain) = domainValue;
    sle->at(sfTickSize) = tickSizeValue;
    sle->at(sfTicketCount) = ticketCountValue;
    sle->at(sfNFTokenMinter) = nFTokenMinterValue;
    sle->at(sfMintedNFTokens) = mintedNFTokensValue;
    sle->at(sfBurnedNFTokens) = burnedNFTokensValue;
    sle->at(sfFirstNFTokenSequence) = firstNFTokenSequenceValue;
    sle->at(sfAMMID) = aMMIDValue;
    sle->at(sfVaultID) = vaultIDValue;
    sle->at(sfLoanBrokerID) = loanBrokerIDValue;

    AccountRootBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AccountRoot entryFromSle{sle};
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
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = balanceValue;

        auto const fromSle = entryFromSle.getBalance();
        auto const fromBuilder = entryFromBuilder.getBalance();

        expectEqualField(expected, fromSle, "sfBalance");
        expectEqualField(expected, fromBuilder, "sfBalance");
    }

    {
        auto const& expected = ownerCountValue;

        auto const fromSle = entryFromSle.getOwnerCount();
        auto const fromBuilder = entryFromBuilder.getOwnerCount();

        expectEqualField(expected, fromSle, "sfOwnerCount");
        expectEqualField(expected, fromBuilder, "sfOwnerCount");
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
        auto const& expected = accountTxnIDValue;

        auto const fromSleOpt = entryFromSle.getAccountTxnID();
        auto const fromBuilderOpt = entryFromBuilder.getAccountTxnID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAccountTxnID");
        expectEqualField(expected, *fromBuilderOpt, "sfAccountTxnID");
    }

    {
        auto const& expected = regularKeyValue;

        auto const fromSleOpt = entryFromSle.getRegularKey();
        auto const fromBuilderOpt = entryFromBuilder.getRegularKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfRegularKey");
        expectEqualField(expected, *fromBuilderOpt, "sfRegularKey");
    }

    {
        auto const& expected = emailHashValue;

        auto const fromSleOpt = entryFromSle.getEmailHash();
        auto const fromBuilderOpt = entryFromBuilder.getEmailHash();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfEmailHash");
        expectEqualField(expected, *fromBuilderOpt, "sfEmailHash");
    }

    {
        auto const& expected = walletLocatorValue;

        auto const fromSleOpt = entryFromSle.getWalletLocator();
        auto const fromBuilderOpt = entryFromBuilder.getWalletLocator();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfWalletLocator");
        expectEqualField(expected, *fromBuilderOpt, "sfWalletLocator");
    }

    {
        auto const& expected = walletSizeValue;

        auto const fromSleOpt = entryFromSle.getWalletSize();
        auto const fromBuilderOpt = entryFromBuilder.getWalletSize();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfWalletSize");
        expectEqualField(expected, *fromBuilderOpt, "sfWalletSize");
    }

    {
        auto const& expected = messageKeyValue;

        auto const fromSleOpt = entryFromSle.getMessageKey();
        auto const fromBuilderOpt = entryFromBuilder.getMessageKey();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMessageKey");
        expectEqualField(expected, *fromBuilderOpt, "sfMessageKey");
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
        auto const& expected = domainValue;

        auto const fromSleOpt = entryFromSle.getDomain();
        auto const fromBuilderOpt = entryFromBuilder.getDomain();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDomain");
        expectEqualField(expected, *fromBuilderOpt, "sfDomain");
    }

    {
        auto const& expected = tickSizeValue;

        auto const fromSleOpt = entryFromSle.getTickSize();
        auto const fromBuilderOpt = entryFromBuilder.getTickSize();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTickSize");
        expectEqualField(expected, *fromBuilderOpt, "sfTickSize");
    }

    {
        auto const& expected = ticketCountValue;

        auto const fromSleOpt = entryFromSle.getTicketCount();
        auto const fromBuilderOpt = entryFromBuilder.getTicketCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTicketCount");
        expectEqualField(expected, *fromBuilderOpt, "sfTicketCount");
    }

    {
        auto const& expected = nFTokenMinterValue;

        auto const fromSleOpt = entryFromSle.getNFTokenMinter();
        auto const fromBuilderOpt = entryFromBuilder.getNFTokenMinter();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfNFTokenMinter");
        expectEqualField(expected, *fromBuilderOpt, "sfNFTokenMinter");
    }

    {
        auto const& expected = mintedNFTokensValue;

        auto const fromSleOpt = entryFromSle.getMintedNFTokens();
        auto const fromBuilderOpt = entryFromBuilder.getMintedNFTokens();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMintedNFTokens");
        expectEqualField(expected, *fromBuilderOpt, "sfMintedNFTokens");
    }

    {
        auto const& expected = burnedNFTokensValue;

        auto const fromSleOpt = entryFromSle.getBurnedNFTokens();
        auto const fromBuilderOpt = entryFromBuilder.getBurnedNFTokens();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBurnedNFTokens");
        expectEqualField(expected, *fromBuilderOpt, "sfBurnedNFTokens");
    }

    {
        auto const& expected = firstNFTokenSequenceValue;

        auto const fromSleOpt = entryFromSle.getFirstNFTokenSequence();
        auto const fromBuilderOpt = entryFromBuilder.getFirstNFTokenSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFirstNFTokenSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfFirstNFTokenSequence");
    }

    {
        auto const& expected = aMMIDValue;

        auto const fromSleOpt = entryFromSle.getAMMID();
        auto const fromBuilderOpt = entryFromBuilder.getAMMID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAMMID");
        expectEqualField(expected, *fromBuilderOpt, "sfAMMID");
    }

    {
        auto const& expected = vaultIDValue;

        auto const fromSleOpt = entryFromSle.getVaultID();
        auto const fromBuilderOpt = entryFromBuilder.getVaultID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultID");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultID");
    }

    {
        auto const& expected = loanBrokerIDValue;

        auto const fromSleOpt = entryFromSle.getLoanBrokerID();
        auto const fromBuilderOpt = entryFromBuilder.getLoanBrokerID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLoanBrokerID");
        expectEqualField(expected, *fromBuilderOpt, "sfLoanBrokerID");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(AccountRootTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AccountRoot{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AccountRootTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AccountRootBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(AccountRootTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const balanceValue = canonical_AMOUNT();
    auto const ownerCountValue = canonical_UINT32();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    AccountRootBuilder builder{
        accountValue,
        sequenceValue,
        balanceValue,
        ownerCountValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAccountTxnID());
    EXPECT_FALSE(entry.getAccountTxnID().has_value());
    EXPECT_FALSE(entry.hasRegularKey());
    EXPECT_FALSE(entry.getRegularKey().has_value());
    EXPECT_FALSE(entry.hasEmailHash());
    EXPECT_FALSE(entry.getEmailHash().has_value());
    EXPECT_FALSE(entry.hasWalletLocator());
    EXPECT_FALSE(entry.getWalletLocator().has_value());
    EXPECT_FALSE(entry.hasWalletSize());
    EXPECT_FALSE(entry.getWalletSize().has_value());
    EXPECT_FALSE(entry.hasMessageKey());
    EXPECT_FALSE(entry.getMessageKey().has_value());
    EXPECT_FALSE(entry.hasTransferRate());
    EXPECT_FALSE(entry.getTransferRate().has_value());
    EXPECT_FALSE(entry.hasDomain());
    EXPECT_FALSE(entry.getDomain().has_value());
    EXPECT_FALSE(entry.hasTickSize());
    EXPECT_FALSE(entry.getTickSize().has_value());
    EXPECT_FALSE(entry.hasTicketCount());
    EXPECT_FALSE(entry.getTicketCount().has_value());
    EXPECT_FALSE(entry.hasNFTokenMinter());
    EXPECT_FALSE(entry.getNFTokenMinter().has_value());
    EXPECT_FALSE(entry.hasMintedNFTokens());
    EXPECT_FALSE(entry.getMintedNFTokens().has_value());
    EXPECT_FALSE(entry.hasBurnedNFTokens());
    EXPECT_FALSE(entry.getBurnedNFTokens().has_value());
    EXPECT_FALSE(entry.hasFirstNFTokenSequence());
    EXPECT_FALSE(entry.getFirstNFTokenSequence().has_value());
    EXPECT_FALSE(entry.hasAMMID());
    EXPECT_FALSE(entry.getAMMID().has_value());
    EXPECT_FALSE(entry.hasVaultID());
    EXPECT_FALSE(entry.getVaultID().has_value());
    EXPECT_FALSE(entry.hasLoanBrokerID());
    EXPECT_FALSE(entry.getLoanBrokerID().has_value());
}
}
