// Auto-generated unit tests for transaction UNLModify


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/UNLModify.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsUNLModifyTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testUNLModify"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const uNLModifyDisablingValue = canonical_UINT8();
    auto const ledgerSequenceValue = canonical_UINT32();
    auto const uNLModifyValidatorValue = canonical_VL();

    UNLModifyBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setUNLModifyDisabling(uNLModifyDisablingValue);
    builder.setLedgerSequence(ledgerSequenceValue);
    builder.setUNLModifyValidator(uNLModifyValidatorValue);

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
        auto const& expected = uNLModifyDisablingValue;
        auto const actualOpt = tx.getUNLModifyDisabling();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUNLModifyDisabling should be present";
        expectEqualField(expected, *actualOpt, "sfUNLModifyDisabling");
        EXPECT_TRUE(tx.hasUNLModifyDisabling());
    }

    {
        auto const& expected = ledgerSequenceValue;
        auto const actualOpt = tx.getLedgerSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLedgerSequence should be present";
        expectEqualField(expected, *actualOpt, "sfLedgerSequence");
        EXPECT_TRUE(tx.hasLedgerSequence());
    }

    {
        auto const& expected = uNLModifyValidatorValue;
        auto const actualOpt = tx.getUNLModifyValidator();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUNLModifyValidator should be present";
        expectEqualField(expected, *actualOpt, "sfUNLModifyValidator");
        EXPECT_TRUE(tx.hasUNLModifyValidator());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsUNLModifyTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testUNLModifyFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const uNLModifyDisablingValue = canonical_UINT8();
    auto const ledgerSequenceValue = canonical_UINT32();
    auto const uNLModifyValidatorValue = canonical_VL();

    // Build an initial transaction
    UNLModifyBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setUNLModifyDisabling(uNLModifyDisablingValue);
    initialBuilder.setLedgerSequence(ledgerSequenceValue);
    initialBuilder.setUNLModifyValidator(uNLModifyValidatorValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    UNLModifyBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = uNLModifyDisablingValue;
        auto const actualOpt = rebuiltTx.getUNLModifyDisabling();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUNLModifyDisabling should be present";
        expectEqualField(expected, *actualOpt, "sfUNLModifyDisabling");
    }

    {
        auto const& expected = ledgerSequenceValue;
        auto const actualOpt = rebuiltTx.getLedgerSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLedgerSequence should be present";
        expectEqualField(expected, *actualOpt, "sfLedgerSequence");
    }

    {
        auto const& expected = uNLModifyValidatorValue;
        auto const actualOpt = rebuiltTx.getUNLModifyValidator();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUNLModifyValidator should be present";
        expectEqualField(expected, *actualOpt, "sfUNLModifyValidator");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsUNLModifyTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(UNLModify{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsUNLModifyTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(UNLModifyBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsUNLModifyTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testUNLModifyNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    UNLModifyBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasUNLModifyDisabling());
    EXPECT_FALSE(tx.getUNLModifyDisabling().has_value());
    EXPECT_FALSE(tx.hasLedgerSequence());
    EXPECT_FALSE(tx.getLedgerSequence().has_value());
    EXPECT_FALSE(tx.hasUNLModifyValidator());
    EXPECT_FALSE(tx.getUNLModifyValidator().has_value());
}

}
