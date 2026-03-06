// Auto-generated unit tests for ledger entry DepositPreauth

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/DepositPreauth.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(DepositPreauthTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const authorizeValue = canonical_ACCOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const authorizeCredentialsValue = canonical_ARRAY();

    DepositPreauthBuilder builder{
        accountValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setAuthorize(authorizeValue);
    builder.setAuthorizeCredentials(authorizeCredentialsValue);

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
        auto const& expected = authorizeValue;
        auto const actualOpt = entry->getAuthorize();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuthorize");
        EXPECT_TRUE(entry->hasAuthorize());
    }

    {
        auto const& expected = authorizeCredentialsValue;
        auto const actualOpt = entry->getAuthorizeCredentials();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuthorizeCredentials");
        EXPECT_TRUE(entry->hasAuthorizeCredentials());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(DepositPreauthTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const authorizeValue = canonical_ACCOUNT();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const authorizeCredentialsValue = canonical_ARRAY();

    SLE sle{DepositPreauth::entryType, index};

    sle[sfAccount] = accountValue;
    sle[sfAuthorize] = authorizeValue;
    sle[sfOwnerNode] = ownerNodeValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;
    sle.setFieldArray(sfAuthorizeCredentials, authorizeCredentialsValue);

    DepositPreauthBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    DepositPreauth entryFromSle{sle};
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
        auto const& expected = authorizeValue;

        auto const fromSleOpt = entryFromSle.getAuthorize();
        auto const fromBuilderOpt = entryFromBuilder->getAuthorize();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuthorize");
        expectEqualField(expected, *fromBuilderOpt, "sfAuthorize");
    }

    {
        auto const& expected = authorizeCredentialsValue;

        auto const fromSleOpt = entryFromSle.getAuthorizeCredentials();
        auto const fromBuilderOpt = entryFromBuilder->getAuthorizeCredentials();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuthorizeCredentials");
        expectEqualField(expected, *fromBuilderOpt, "sfAuthorizeCredentials");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
