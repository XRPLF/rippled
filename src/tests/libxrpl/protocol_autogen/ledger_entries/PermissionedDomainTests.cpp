// Auto-generated unit tests for ledger entry PermissionedDomain


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/PermissionedDomain.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(PermissionedDomainTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const acceptedCredentialsValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    PermissionedDomainBuilder builder{
        ownerValue,
        sequenceValue,
        acceptedCredentialsValue,
        ownerNodeValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };


    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = acceptedCredentialsValue;
        auto const actual = entry.getAcceptedCredentials();
        expectEqualField(expected, actual, "sfAcceptedCredentials");
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

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(PermissionedDomainTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const acceptedCredentialsValue = canonical_ARRAY();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(PermissionedDomain::entryType, index);

    sle->at(sfOwner) = ownerValue;
    sle->at(sfSequence) = sequenceValue;
    sle->setFieldArray(sfAcceptedCredentials, acceptedCredentialsValue);
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    PermissionedDomainBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    PermissionedDomain entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = acceptedCredentialsValue;

        auto const fromSle = entryFromSle.getAcceptedCredentials();
        auto const fromBuilder = entryFromBuilder.getAcceptedCredentials();

        expectEqualField(expected, fromSle, "sfAcceptedCredentials");
        expectEqualField(expected, fromBuilder, "sfAcceptedCredentials");
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

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(PermissionedDomainTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(PermissionedDomain{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(PermissionedDomainTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(PermissionedDomainBuilder{wrongEntry.getSle()}, std::runtime_error);
}

}
