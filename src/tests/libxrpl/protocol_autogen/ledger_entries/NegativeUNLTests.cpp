// Auto-generated unit tests for ledger entry NegativeUNL


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/NegativeUNL.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(NegativeUNLTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const disabledValidatorsValue = canonical_ARRAY();
    auto const validatorToDisableValue = canonical_VL();
    auto const validatorToReEnableValue = canonical_VL();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    NegativeUNLBuilder builder{
    };

    builder.setDisabledValidators(disabledValidatorsValue);
    builder.setValidatorToDisable(validatorToDisableValue);
    builder.setValidatorToReEnable(validatorToReEnableValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = disabledValidatorsValue;
        auto const actualOpt = entry.getDisabledValidators();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDisabledValidators");
        EXPECT_TRUE(entry.hasDisabledValidators());
    }

    {
        auto const& expected = validatorToDisableValue;
        auto const actualOpt = entry.getValidatorToDisable();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfValidatorToDisable");
        EXPECT_TRUE(entry.hasValidatorToDisable());
    }

    {
        auto const& expected = validatorToReEnableValue;
        auto const actualOpt = entry.getValidatorToReEnable();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfValidatorToReEnable");
        EXPECT_TRUE(entry.hasValidatorToReEnable());
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
TEST(NegativeUNLTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const disabledValidatorsValue = canonical_ARRAY();
    auto const validatorToDisableValue = canonical_VL();
    auto const validatorToReEnableValue = canonical_VL();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(NegativeUNL::entryType, index);

    sle->setFieldArray(sfDisabledValidators, disabledValidatorsValue);
    sle->at(sfValidatorToDisable) = validatorToDisableValue;
    sle->at(sfValidatorToReEnable) = validatorToReEnableValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    NegativeUNLBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    NegativeUNL entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = disabledValidatorsValue;

        auto const fromSleOpt = entryFromSle.getDisabledValidators();
        auto const fromBuilderOpt = entryFromBuilder.getDisabledValidators();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDisabledValidators");
        expectEqualField(expected, *fromBuilderOpt, "sfDisabledValidators");
    }

    {
        auto const& expected = validatorToDisableValue;

        auto const fromSleOpt = entryFromSle.getValidatorToDisable();
        auto const fromBuilderOpt = entryFromBuilder.getValidatorToDisable();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfValidatorToDisable");
        expectEqualField(expected, *fromBuilderOpt, "sfValidatorToDisable");
    }

    {
        auto const& expected = validatorToReEnableValue;

        auto const fromSleOpt = entryFromSle.getValidatorToReEnable();
        auto const fromBuilderOpt = entryFromBuilder.getValidatorToReEnable();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfValidatorToReEnable");
        expectEqualField(expected, *fromBuilderOpt, "sfValidatorToReEnable");
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
TEST(NegativeUNLTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(NegativeUNL{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(NegativeUNLTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(NegativeUNLBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(NegativeUNLTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    NegativeUNLBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasDisabledValidators());
    EXPECT_FALSE(entry.getDisabledValidators().has_value());
    EXPECT_FALSE(entry.hasValidatorToDisable());
    EXPECT_FALSE(entry.getValidatorToDisable().has_value());
    EXPECT_FALSE(entry.hasValidatorToReEnable());
    EXPECT_FALSE(entry.getValidatorToReEnable().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
