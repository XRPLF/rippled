// Auto-generated unit tests for transaction LedgerStateFix


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LedgerStateFix.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLedgerStateFixTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLedgerStateFix"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ledgerFixTypeValue = canonical_UINT16();
    auto const ownerValue = canonical_ACCOUNT();

    LedgerStateFixBuilder builder{
        accountValue,
        ledgerFixTypeValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setOwner(ownerValue);

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
    {
        auto const& expected = ledgerFixTypeValue;
        auto const actual = tx.getLedgerFixType();
        expectEqualField(expected, actual, "sfLedgerFixType");
    }

    // Verify optional fields
    {
        auto const& expected = ownerValue;
        auto const actualOpt = tx.getOwner();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwner should be present";
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(tx.hasOwner());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsLedgerStateFixTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLedgerStateFixFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ledgerFixTypeValue = canonical_UINT16();
    auto const ownerValue = canonical_ACCOUNT();

    // Build an initial transaction
    LedgerStateFixBuilder initialBuilder{
        accountValue,
        ledgerFixTypeValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setOwner(ownerValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LedgerStateFixBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = ledgerFixTypeValue;
        auto const actual = rebuiltTx.getLedgerFixType();
        expectEqualField(expected, actual, "sfLedgerFixType");
    }

    // Verify optional fields
    {
        auto const& expected = ownerValue;
        auto const actualOpt = rebuiltTx.getOwner();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwner should be present";
        expectEqualField(expected, *actualOpt, "sfOwner");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsLedgerStateFixTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(LedgerStateFix{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsLedgerStateFixTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(LedgerStateFixBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsLedgerStateFixTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLedgerStateFixNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const ledgerFixTypeValue = canonical_UINT16();

    LedgerStateFixBuilder builder{
        accountValue,
        ledgerFixTypeValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasOwner());
    EXPECT_FALSE(tx.getOwner().has_value());
}

}
