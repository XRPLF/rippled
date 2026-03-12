// Auto-generated unit tests for transaction SetFee


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/SetFee.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsSetFeeTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSetFee"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ledgerSequenceValue = canonical_UINT32();
    auto const baseFeeValue = canonical_UINT64();
    auto const referenceFeeUnitsValue = canonical_UINT32();
    auto const reserveBaseValue = canonical_UINT32();
    auto const reserveIncrementValue = canonical_UINT32();
    auto const baseFeeDropsValue = canonical_AMOUNT();
    auto const reserveBaseDropsValue = canonical_AMOUNT();
    auto const reserveIncrementDropsValue = canonical_AMOUNT();

    SetFeeBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setLedgerSequence(ledgerSequenceValue);
    builder.setBaseFee(baseFeeValue);
    builder.setReferenceFeeUnits(referenceFeeUnitsValue);
    builder.setReserveBase(reserveBaseValue);
    builder.setReserveIncrement(reserveIncrementValue);
    builder.setBaseFeeDrops(baseFeeDropsValue);
    builder.setReserveBaseDrops(reserveBaseDropsValue);
    builder.setReserveIncrementDrops(reserveIncrementDropsValue);

    auto tx = builder.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(tx.validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx.getSigningPubKey().empty());
    EXPECT_TRUE(tx.hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx.getAccount(), accountValue);
    EXPECT_EQ(tx.getSequence(), sequenceValue);
    EXPECT_EQ(tx.getFee(), feeValue);

    // Verify required fields
    // Verify optional fields
    {
        auto const& expected = ledgerSequenceValue;
        auto const actualOpt = tx.getLedgerSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLedgerSequence should be present";
        expectEqualField(expected, *actualOpt, "sfLedgerSequence");
        EXPECT_TRUE(tx.hasLedgerSequence());
    }

    {
        auto const& expected = baseFeeValue;
        auto const actualOpt = tx.getBaseFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBaseFee should be present";
        expectEqualField(expected, *actualOpt, "sfBaseFee");
        EXPECT_TRUE(tx.hasBaseFee());
    }

    {
        auto const& expected = referenceFeeUnitsValue;
        auto const actualOpt = tx.getReferenceFeeUnits();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReferenceFeeUnits should be present";
        expectEqualField(expected, *actualOpt, "sfReferenceFeeUnits");
        EXPECT_TRUE(tx.hasReferenceFeeUnits());
    }

    {
        auto const& expected = reserveBaseValue;
        auto const actualOpt = tx.getReserveBase();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveBase should be present";
        expectEqualField(expected, *actualOpt, "sfReserveBase");
        EXPECT_TRUE(tx.hasReserveBase());
    }

    {
        auto const& expected = reserveIncrementValue;
        auto const actualOpt = tx.getReserveIncrement();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveIncrement should be present";
        expectEqualField(expected, *actualOpt, "sfReserveIncrement");
        EXPECT_TRUE(tx.hasReserveIncrement());
    }

    {
        auto const& expected = baseFeeDropsValue;
        auto const actualOpt = tx.getBaseFeeDrops();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBaseFeeDrops should be present";
        expectEqualField(expected, *actualOpt, "sfBaseFeeDrops");
        EXPECT_TRUE(tx.hasBaseFeeDrops());
    }

    {
        auto const& expected = reserveBaseDropsValue;
        auto const actualOpt = tx.getReserveBaseDrops();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveBaseDrops should be present";
        expectEqualField(expected, *actualOpt, "sfReserveBaseDrops");
        EXPECT_TRUE(tx.hasReserveBaseDrops());
    }

    {
        auto const& expected = reserveIncrementDropsValue;
        auto const actualOpt = tx.getReserveIncrementDrops();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveIncrementDrops should be present";
        expectEqualField(expected, *actualOpt, "sfReserveIncrementDrops");
        EXPECT_TRUE(tx.hasReserveIncrementDrops());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsSetFeeTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSetFeeFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ledgerSequenceValue = canonical_UINT32();
    auto const baseFeeValue = canonical_UINT64();
    auto const referenceFeeUnitsValue = canonical_UINT32();
    auto const reserveBaseValue = canonical_UINT32();
    auto const reserveIncrementValue = canonical_UINT32();
    auto const baseFeeDropsValue = canonical_AMOUNT();
    auto const reserveBaseDropsValue = canonical_AMOUNT();
    auto const reserveIncrementDropsValue = canonical_AMOUNT();

    // Build an initial transaction
    SetFeeBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setLedgerSequence(ledgerSequenceValue);
    initialBuilder.setBaseFee(baseFeeValue);
    initialBuilder.setReferenceFeeUnits(referenceFeeUnitsValue);
    initialBuilder.setReserveBase(reserveBaseValue);
    initialBuilder.setReserveIncrement(reserveIncrementValue);
    initialBuilder.setBaseFeeDrops(baseFeeDropsValue);
    initialBuilder.setReserveBaseDrops(reserveBaseDropsValue);
    initialBuilder.setReserveIncrementDrops(reserveIncrementDropsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    SetFeeBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    // Verify optional fields
    {
        auto const& expected = ledgerSequenceValue;
        auto const actualOpt = rebuiltTx.getLedgerSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLedgerSequence should be present";
        expectEqualField(expected, *actualOpt, "sfLedgerSequence");
    }

    {
        auto const& expected = baseFeeValue;
        auto const actualOpt = rebuiltTx.getBaseFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBaseFee should be present";
        expectEqualField(expected, *actualOpt, "sfBaseFee");
    }

    {
        auto const& expected = referenceFeeUnitsValue;
        auto const actualOpt = rebuiltTx.getReferenceFeeUnits();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReferenceFeeUnits should be present";
        expectEqualField(expected, *actualOpt, "sfReferenceFeeUnits");
    }

    {
        auto const& expected = reserveBaseValue;
        auto const actualOpt = rebuiltTx.getReserveBase();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveBase should be present";
        expectEqualField(expected, *actualOpt, "sfReserveBase");
    }

    {
        auto const& expected = reserveIncrementValue;
        auto const actualOpt = rebuiltTx.getReserveIncrement();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveIncrement should be present";
        expectEqualField(expected, *actualOpt, "sfReserveIncrement");
    }

    {
        auto const& expected = baseFeeDropsValue;
        auto const actualOpt = rebuiltTx.getBaseFeeDrops();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBaseFeeDrops should be present";
        expectEqualField(expected, *actualOpt, "sfBaseFeeDrops");
    }

    {
        auto const& expected = reserveBaseDropsValue;
        auto const actualOpt = rebuiltTx.getReserveBaseDrops();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveBaseDrops should be present";
        expectEqualField(expected, *actualOpt, "sfReserveBaseDrops");
    }

    {
        auto const& expected = reserveIncrementDropsValue;
        auto const actualOpt = rebuiltTx.getReserveIncrementDrops();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveIncrementDrops should be present";
        expectEqualField(expected, *actualOpt, "sfReserveIncrementDrops");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsSetFeeTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SetFee{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsSetFeeTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SetFeeBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsSetFeeTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSetFeeNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    SetFeeBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasLedgerSequence());
    EXPECT_FALSE(tx.getLedgerSequence().has_value());
    EXPECT_FALSE(tx.hasBaseFee());
    EXPECT_FALSE(tx.getBaseFee().has_value());
    EXPECT_FALSE(tx.hasReferenceFeeUnits());
    EXPECT_FALSE(tx.getReferenceFeeUnits().has_value());
    EXPECT_FALSE(tx.hasReserveBase());
    EXPECT_FALSE(tx.getReserveBase().has_value());
    EXPECT_FALSE(tx.hasReserveIncrement());
    EXPECT_FALSE(tx.getReserveIncrement().has_value());
    EXPECT_FALSE(tx.hasBaseFeeDrops());
    EXPECT_FALSE(tx.getBaseFeeDrops().has_value());
    EXPECT_FALSE(tx.hasReserveBaseDrops());
    EXPECT_FALSE(tx.getReserveBaseDrops().has_value());
    EXPECT_FALSE(tx.hasReserveIncrementDrops());
    EXPECT_FALSE(tx.getReserveIncrementDrops().has_value());
}

}
