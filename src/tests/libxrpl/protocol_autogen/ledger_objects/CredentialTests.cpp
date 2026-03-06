// Auto-generated unit tests for ledger entry Credential

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_objects/Credential.h>

#include <string>

namespace xrpl::ledger_entries {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(CredentialTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const subjectValue = canonical_ACCOUNT();
    auto const issuerValue = canonical_ACCOUNT();
    auto const credentialTypeValue = canonical_VL();
    auto const expirationValue = canonical_UINT32();
    auto const uRIValue = canonical_VL();
    auto const issuerNodeValue = canonical_UINT64();
    auto const subjectNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    CredentialBuilder builder{
        subjectValue,
        issuerValue,
        credentialTypeValue,
        issuerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setExpiration(expirationValue);
    builder.setURI(uRIValue);
    builder.setSubjectNode(subjectNodeValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry->validate());

    {
        auto const& expected = subjectValue;
        auto const actual = entry->getSubject();
        expectEqualField(expected, actual, "sfSubject");
    }

    {
        auto const& expected = issuerValue;
        auto const actual = entry->getIssuer();
        expectEqualField(expected, actual, "sfIssuer");
    }

    {
        auto const& expected = credentialTypeValue;
        auto const actual = entry->getCredentialType();
        expectEqualField(expected, actual, "sfCredentialType");
    }

    {
        auto const& expected = issuerNodeValue;
        auto const actual = entry->getIssuerNode();
        expectEqualField(expected, actual, "sfIssuerNode");
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
        auto const& expected = uRIValue;
        auto const actualOpt = entry->getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry->hasURI());
    }

    {
        auto const& expected = subjectNodeValue;
        auto const actualOpt = entry->getSubjectNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSubjectNode");
        EXPECT_TRUE(entry->hasSubjectNode());
    }

    EXPECT_TRUE(entry->hasLedgerIndex());
    auto const ledgerIndex = entry->getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry->getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(CredentialTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const subjectValue = canonical_ACCOUNT();
    auto const issuerValue = canonical_ACCOUNT();
    auto const credentialTypeValue = canonical_VL();
    auto const expirationValue = canonical_UINT32();
    auto const uRIValue = canonical_VL();
    auto const issuerNodeValue = canonical_UINT64();
    auto const subjectNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    SLE sle{Credential::entryType, index};

    sle[sfSubject] = subjectValue;
    sle[sfIssuer] = issuerValue;
    sle[sfCredentialType] = credentialTypeValue;
    sle[sfExpiration] = expirationValue;
    sle[sfURI] = uRIValue;
    sle[sfIssuerNode] = issuerNodeValue;
    sle[sfSubjectNode] = subjectNodeValue;
    sle[sfPreviousTxnID] = previousTxnIDValue;
    sle[sfPreviousTxnLgrSeq] = previousTxnLgrSeqValue;

    CredentialBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Credential entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder->validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = subjectValue;

        auto const fromSle = entryFromSle.getSubject();
        auto const fromBuilder = entryFromBuilder->getSubject();

        expectEqualField(expected, fromSle, "sfSubject");
        expectEqualField(expected, fromBuilder, "sfSubject");
    }

    {
        auto const& expected = issuerValue;

        auto const fromSle = entryFromSle.getIssuer();
        auto const fromBuilder = entryFromBuilder->getIssuer();

        expectEqualField(expected, fromSle, "sfIssuer");
        expectEqualField(expected, fromBuilder, "sfIssuer");
    }

    {
        auto const& expected = credentialTypeValue;

        auto const fromSle = entryFromSle.getCredentialType();
        auto const fromBuilder = entryFromBuilder->getCredentialType();

        expectEqualField(expected, fromSle, "sfCredentialType");
        expectEqualField(expected, fromBuilder, "sfCredentialType");
    }

    {
        auto const& expected = issuerNodeValue;

        auto const fromSle = entryFromSle.getIssuerNode();
        auto const fromBuilder = entryFromBuilder->getIssuerNode();

        expectEqualField(expected, fromSle, "sfIssuerNode");
        expectEqualField(expected, fromBuilder, "sfIssuerNode");
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
        auto const& expected = uRIValue;

        auto const fromSleOpt = entryFromSle.getURI();
        auto const fromBuilderOpt = entryFromBuilder->getURI();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfURI");
        expectEqualField(expected, *fromBuilderOpt, "sfURI");
    }

    {
        auto const& expected = subjectNodeValue;

        auto const fromSleOpt = entryFromSle.getSubjectNode();
        auto const fromBuilderOpt = entryFromBuilder->getSubjectNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSubjectNode");
        expectEqualField(expected, *fromBuilderOpt, "sfSubjectNode");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder->getKey(), index);
}
}
