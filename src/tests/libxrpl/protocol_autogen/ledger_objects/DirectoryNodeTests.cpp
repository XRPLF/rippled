// Auto-generated unit tests for ledger entry DirectoryNode

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/DirectoryNode.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(DirectoryNodeTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const takerPaysCurrencyValue = canonical_UINT160();
    auto const takerPaysIssuerValue = canonical_UINT160();
    auto const takerGetsCurrencyValue = canonical_UINT160();
    auto const takerGetsIssuerValue = canonical_UINT160();
    auto const exchangeRateValue = canonical_UINT64();
    auto const indexesValue = canonical_VECTOR256();
    auto const rootIndexValue = canonical_UINT256();
    auto const indexNextValue = canonical_UINT64();
    auto const indexPreviousValue = canonical_UINT64();
    auto const nFTokenIDValue = canonical_UINT256();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();

    DirectoryNodeBuilder builder{
        indexesValue,
        rootIndexValue
    };

    builder.setOwner(ownerValue);
    builder.setTakerPaysCurrency(takerPaysCurrencyValue);
    builder.setTakerPaysIssuer(takerPaysIssuerValue);
    builder.setTakerGetsCurrency(takerGetsCurrencyValue);
    builder.setTakerGetsIssuer(takerGetsIssuerValue);
    builder.setExchangeRate(exchangeRateValue);
    builder.setIndexNext(indexNextValue);
    builder.setIndexPrevious(indexPreviousValue);
    builder.setNFTokenID(nFTokenIDValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);
    builder.setDomainID(domainIDValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry->validate());

    {
        auto const& expected = indexesValue;
        auto const actual = entry->getIndexes();
        expectEqualField(expected, actual, "sfIndexes");
    }

    {
        auto const& expected = rootIndexValue;
        auto const actual = entry->getRootIndex();
        expectEqualField(expected, actual, "sfRootIndex");
    }

    {
        auto const& expected = ownerValue;
        auto const actualOpt = entry->getOwner();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(entry->hasOwner());
    }

    {
        auto const& expected = takerPaysCurrencyValue;
        auto const actualOpt = entry->getTakerPaysCurrency();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTakerPaysCurrency");
        EXPECT_TRUE(entry->hasTakerPaysCurrency());
    }

    {
        auto const& expected = takerPaysIssuerValue;
        auto const actualOpt = entry->getTakerPaysIssuer();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTakerPaysIssuer");
        EXPECT_TRUE(entry->hasTakerPaysIssuer());
    }

    {
        auto const& expected = takerGetsCurrencyValue;
        auto const actualOpt = entry->getTakerGetsCurrency();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTakerGetsCurrency");
        EXPECT_TRUE(entry->hasTakerGetsCurrency());
    }

    {
        auto const& expected = takerGetsIssuerValue;
        auto const actualOpt = entry->getTakerGetsIssuer();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTakerGetsIssuer");
        EXPECT_TRUE(entry->hasTakerGetsIssuer());
    }

    {
        auto const& expected = exchangeRateValue;
        auto const actualOpt = entry->getExchangeRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExchangeRate");
        EXPECT_TRUE(entry->hasExchangeRate());
    }

    {
        auto const& expected = indexNextValue;
        auto const actualOpt = entry->getIndexNext();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIndexNext");
        EXPECT_TRUE(entry->hasIndexNext());
    }

    {
        auto const& expected = indexPreviousValue;
        auto const actualOpt = entry->getIndexPrevious();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIndexPrevious");
        EXPECT_TRUE(entry->hasIndexPrevious());
    }

    {
        auto const& expected = nFTokenIDValue;
        auto const actualOpt = entry->getNFTokenID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfNFTokenID");
        EXPECT_TRUE(entry->hasNFTokenID());
    }

    {
        auto const& expected = previousTxnIDValue;
        auto const actualOpt = entry->getPreviousTxnID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousTxnID");
        EXPECT_TRUE(entry->hasPreviousTxnID());
    }

    {
        auto const& expected = previousTxnLgrSeqValue;
        auto const actualOpt = entry->getPreviousTxnLgrSeq();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousTxnLgrSeq");
        EXPECT_TRUE(entry->hasPreviousTxnLgrSeq());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = entry->getDomainID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(entry->hasDomainID());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(DirectoryNodeTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const takerPaysCurrencyValue = canonical_UINT160();
    auto const takerPaysIssuerValue = canonical_UINT160();
    auto const takerGetsCurrencyValue = canonical_UINT160();
    auto const takerGetsIssuerValue = canonical_UINT160();
    auto const exchangeRateValue = canonical_UINT64();
    auto const indexesValue = canonical_VECTOR256();
    auto const rootIndexValue = canonical_UINT256();
    auto const indexNextValue = canonical_UINT64();
    auto const indexPreviousValue = canonical_UINT64();
    auto const nFTokenIDValue = canonical_UINT256();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();

    SLE sle{DirectoryNode::entryType, index};

    sle[sfOwner] = ownerValue;
    sle[sfTakerPaysCurrency] = takerPaysCurrencyValue;
    sle[sfTakerPaysIssuer] = takerPaysIssuerValue;
    sle[sfTakerGetsCurrency] = takerGetsCurrencyValue;
    sle[sfTakerGetsIssuer] = takerGetsIssuerValue;
    sle[sfExchangeRate] = exchangeRateValue;
    sle[sfIndexes] = indexesValue;
    sle[sfRootIndex] = rootIndexValue;
    sle[sfIndexNext] = indexNextValue;
    sle[sfIndexPrevious] = indexPreviousValue;
    sle[sfNFTokenID] = nFTokenIDValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;
    sle[sfDomainID] = domainIDValue;

    DirectoryNodeBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    DirectoryNode entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder->validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = indexesValue;

        auto const fromSle = entryFromSle.getIndexes();
        auto const fromBuilder = entryFromBuilder->getIndexes();

        expectEqualField(expected, fromSle, "sfIndexes");
        expectEqualField(expected, fromBuilder, "sfIndexes");
    }

    {
        auto const& expected = rootIndexValue;

        auto const fromSle = entryFromSle.getRootIndex();
        auto const fromBuilder = entryFromBuilder->getRootIndex();

        expectEqualField(expected, fromSle, "sfRootIndex");
        expectEqualField(expected, fromBuilder, "sfRootIndex");
    }

    {
        auto const& expected = ownerValue;

        auto const fromSleOpt = entryFromSle.getOwner();
        auto const fromBuilderOpt = entryFromBuilder->getOwner();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwner");
        expectEqualField(expected, *fromBuilderOpt, "sfOwner");
    }

    {
        auto const& expected = takerPaysCurrencyValue;

        auto const fromSleOpt = entryFromSle.getTakerPaysCurrency();
        auto const fromBuilderOpt = entryFromBuilder->getTakerPaysCurrency();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTakerPaysCurrency");
        expectEqualField(expected, *fromBuilderOpt, "sfTakerPaysCurrency");
    }

    {
        auto const& expected = takerPaysIssuerValue;

        auto const fromSleOpt = entryFromSle.getTakerPaysIssuer();
        auto const fromBuilderOpt = entryFromBuilder->getTakerPaysIssuer();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTakerPaysIssuer");
        expectEqualField(expected, *fromBuilderOpt, "sfTakerPaysIssuer");
    }

    {
        auto const& expected = takerGetsCurrencyValue;

        auto const fromSleOpt = entryFromSle.getTakerGetsCurrency();
        auto const fromBuilderOpt = entryFromBuilder->getTakerGetsCurrency();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTakerGetsCurrency");
        expectEqualField(expected, *fromBuilderOpt, "sfTakerGetsCurrency");
    }

    {
        auto const& expected = takerGetsIssuerValue;

        auto const fromSleOpt = entryFromSle.getTakerGetsIssuer();
        auto const fromBuilderOpt = entryFromBuilder->getTakerGetsIssuer();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTakerGetsIssuer");
        expectEqualField(expected, *fromBuilderOpt, "sfTakerGetsIssuer");
    }

    {
        auto const& expected = exchangeRateValue;

        auto const fromSleOpt = entryFromSle.getExchangeRate();
        auto const fromBuilderOpt = entryFromBuilder->getExchangeRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExchangeRate");
        expectEqualField(expected, *fromBuilderOpt, "sfExchangeRate");
    }

    {
        auto const& expected = indexNextValue;

        auto const fromSleOpt = entryFromSle.getIndexNext();
        auto const fromBuilderOpt = entryFromBuilder->getIndexNext();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIndexNext");
        expectEqualField(expected, *fromBuilderOpt, "sfIndexNext");
    }

    {
        auto const& expected = indexPreviousValue;

        auto const fromSleOpt = entryFromSle.getIndexPrevious();
        auto const fromBuilderOpt = entryFromBuilder->getIndexPrevious();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIndexPrevious");
        expectEqualField(expected, *fromBuilderOpt, "sfIndexPrevious");
    }

    {
        auto const& expected = nFTokenIDValue;

        auto const fromSleOpt = entryFromSle.getNFTokenID();
        auto const fromBuilderOpt = entryFromBuilder->getNFTokenID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfNFTokenID");
        expectEqualField(expected, *fromBuilderOpt, "sfNFTokenID");
    }

    {
        auto const& expected = previousTxnIDValue;

        auto const fromSleOpt = entryFromSle.getPreviousTxnID();
        auto const fromBuilderOpt = entryFromBuilder->getPreviousTxnID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousTxnID");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;

        auto const fromSleOpt = entryFromSle.getPreviousTxnLgrSeq();
        auto const fromBuilderOpt = entryFromBuilder->getPreviousTxnLgrSeq();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousTxnLgrSeq");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousTxnLgrSeq");
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

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
