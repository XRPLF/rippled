// Auto-generated unit tests for ledger entry Credential


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Credential.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

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
    };

    builder.setSubject(subjectValue);
    builder.setIssuer(issuerValue);
    builder.setCredentialType(credentialTypeValue);
    builder.setExpiration(expirationValue);
    builder.setURI(uRIValue);
    builder.setIssuerNode(issuerNodeValue);
    builder.setSubjectNode(subjectNodeValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = subjectValue;
        auto const actualOpt = entry.getSubject();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSubject");
        EXPECT_TRUE(entry.hasSubject());
    }

    {
        auto const& expected = issuerValue;
        auto const actualOpt = entry.getIssuer();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuer");
        EXPECT_TRUE(entry.hasIssuer());
    }

    {
        auto const& expected = credentialTypeValue;
        auto const actualOpt = entry.getCredentialType();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCredentialType");
        EXPECT_TRUE(entry.hasCredentialType());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = entry.getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry.hasURI());
    }

    {
        auto const& expected = issuerNodeValue;
        auto const actualOpt = entry.getIssuerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIssuerNode");
        EXPECT_TRUE(entry.hasIssuerNode());
    }

    {
        auto const& expected = subjectNodeValue;
        auto const actualOpt = entry.getSubjectNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSubjectNode");
        EXPECT_TRUE(entry.hasSubjectNode());
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

    auto sle = std::make_shared<SLE>(Credential::entryType, index);

    sle->at(sfSubject) = subjectValue;
    sle->at(sfIssuer) = issuerValue;
    sle->at(sfCredentialType) = credentialTypeValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfURI) = uRIValue;
    sle->at(sfIssuerNode) = issuerNodeValue;
    sle->at(sfSubjectNode) = subjectNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    CredentialBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Credential entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = subjectValue;

        auto const fromSleOpt = entryFromSle.getSubject();
        auto const fromBuilderOpt = entryFromBuilder.getSubject();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSubject");
        expectEqualField(expected, *fromBuilderOpt, "sfSubject");
    }

    {
        auto const& expected = issuerValue;

        auto const fromSleOpt = entryFromSle.getIssuer();
        auto const fromBuilderOpt = entryFromBuilder.getIssuer();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIssuer");
        expectEqualField(expected, *fromBuilderOpt, "sfIssuer");
    }

    {
        auto const& expected = credentialTypeValue;

        auto const fromSleOpt = entryFromSle.getCredentialType();
        auto const fromBuilderOpt = entryFromBuilder.getCredentialType();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCredentialType");
        expectEqualField(expected, *fromBuilderOpt, "sfCredentialType");
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
        auto const& expected = uRIValue;

        auto const fromSleOpt = entryFromSle.getURI();
        auto const fromBuilderOpt = entryFromBuilder.getURI();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfURI");
        expectEqualField(expected, *fromBuilderOpt, "sfURI");
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

    {
        auto const& expected = subjectNodeValue;

        auto const fromSleOpt = entryFromSle.getSubjectNode();
        auto const fromBuilderOpt = entryFromBuilder.getSubjectNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSubjectNode");
        expectEqualField(expected, *fromBuilderOpt, "sfSubjectNode");
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
TEST(CredentialTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Credential{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(CredentialTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(CredentialBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(CredentialTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    CredentialBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasSubject());
    EXPECT_FALSE(entry.getSubject().has_value());
    EXPECT_FALSE(entry.hasIssuer());
    EXPECT_FALSE(entry.getIssuer().has_value());
    EXPECT_FALSE(entry.hasCredentialType());
    EXPECT_FALSE(entry.getCredentialType().has_value());
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
    EXPECT_FALSE(entry.hasURI());
    EXPECT_FALSE(entry.getURI().has_value());
    EXPECT_FALSE(entry.hasIssuerNode());
    EXPECT_FALSE(entry.getIssuerNode().has_value());
    EXPECT_FALSE(entry.hasSubjectNode());
    EXPECT_FALSE(entry.getSubjectNode().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
