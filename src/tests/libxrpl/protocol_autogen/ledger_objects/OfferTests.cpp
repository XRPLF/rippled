// Auto-generated unit tests for ledger entry Offer

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/Offer.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(OfferTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const takerPaysValue = canonical_AMOUNT();
    auto const takerGetsValue = canonical_AMOUNT();
    auto const bookDirectoryValue = canonical_UINT256();
    auto const bookNodeValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();
    auto const additionalBooksValue = canonical_ARRAY();

    OfferBuilder builder{
        accountValue,
        sequenceValue,
        takerPaysValue,
        takerGetsValue,
        bookDirectoryValue,
        bookNodeValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setExpiration(expirationValue);
    builder.setDomainID(domainIDValue);
    builder.setAdditionalBooks(additionalBooksValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry->validate());

    {
        auto const& expected = accountValue;
        auto const actual = entry->getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = sequenceValue;
        auto const actual = entry->getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = takerPaysValue;
        auto const actual = entry->getTakerPays();
        expectEqualField(expected, actual, "sfTakerPays");
    }

    {
        auto const& expected = takerGetsValue;
        auto const actual = entry->getTakerGets();
        expectEqualField(expected, actual, "sfTakerGets");
    }

    {
        auto const& expected = bookDirectoryValue;
        auto const actual = entry->getBookDirectory();
        expectEqualField(expected, actual, "sfBookDirectory");
    }

    {
        auto const& expected = bookNodeValue;
        auto const actual = entry->getBookNode();
        expectEqualField(expected, actual, "sfBookNode");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry->getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
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
        auto const& expected = expirationValue;
        auto const actualOpt = entry->getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry->hasExpiration());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = entry->getDomainID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(entry->hasDomainID());
    }

    {
        auto const& expected = additionalBooksValue;
        auto const actualOpt = entry->getAdditionalBooks();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAdditionalBooks");
        EXPECT_TRUE(entry->hasAdditionalBooks());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(OfferTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const takerPaysValue = canonical_AMOUNT();
    auto const takerGetsValue = canonical_AMOUNT();
    auto const bookDirectoryValue = canonical_UINT256();
    auto const bookNodeValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();
    auto const additionalBooksValue = canonical_ARRAY();

    SLE sle{Offer::entryType, index};

    sle[sfAccount] = accountValue;
    sle[sfSequence] = sequenceValue;
    sle[sfTakerPays] = takerPaysValue;
    sle[sfTakerGets] = takerGetsValue;
    sle[sfBookDirectory] = bookDirectoryValue;
    sle[sfBookNode] = bookNodeValue;
    sle[sfOwnerNode] = ownerNodeValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;
    sle[sfExpiration] = expirationValue;
    sle[sfDomainID] = domainIDValue;
    sle.setFieldArray(sfAdditionalBooks, additionalBooksValue);

    OfferBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Offer entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder->validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder->getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder->getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = takerPaysValue;

        auto const fromSle = entryFromSle.getTakerPays();
        auto const fromBuilder = entryFromBuilder->getTakerPays();

        expectEqualField(expected, fromSle, "sfTakerPays");
        expectEqualField(expected, fromBuilder, "sfTakerPays");
    }

    {
        auto const& expected = takerGetsValue;

        auto const fromSle = entryFromSle.getTakerGets();
        auto const fromBuilder = entryFromBuilder->getTakerGets();

        expectEqualField(expected, fromSle, "sfTakerGets");
        expectEqualField(expected, fromBuilder, "sfTakerGets");
    }

    {
        auto const& expected = bookDirectoryValue;

        auto const fromSle = entryFromSle.getBookDirectory();
        auto const fromBuilder = entryFromBuilder->getBookDirectory();

        expectEqualField(expected, fromSle, "sfBookDirectory");
        expectEqualField(expected, fromBuilder, "sfBookDirectory");
    }

    {
        auto const& expected = bookNodeValue;

        auto const fromSle = entryFromSle.getBookNode();
        auto const fromBuilder = entryFromBuilder->getBookNode();

        expectEqualField(expected, fromSle, "sfBookNode");
        expectEqualField(expected, fromBuilder, "sfBookNode");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder->getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
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
        auto const& expected = expirationValue;

        auto const fromSleOpt = entryFromSle.getExpiration();
        auto const fromBuilderOpt = entryFromBuilder->getExpiration();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExpiration");
        expectEqualField(expected, *fromBuilderOpt, "sfExpiration");
    }

    {
        auto const& expected = domainIDValue;

        auto const fromSleOpt = entryFromSle.getDomainID();
        auto const fromBuilderOpt = entryFromBuilder->getDomainID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDomainID");
        expectEqualField(expected, *fromBuilderOpt, "sfDomainID");
    }

    {
        auto const& expected = additionalBooksValue;

        auto const fromSleOpt = entryFromSle.getAdditionalBooks();
        auto const fromBuilderOpt = entryFromBuilder->getAdditionalBooks();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAdditionalBooks");
        expectEqualField(expected, *fromBuilderOpt, "sfAdditionalBooks");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
