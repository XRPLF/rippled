// Auto-generated unit tests for transaction CheckCash


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/CheckCash.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsCheckCashTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCheckCash"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const checkIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const deliverMinValue = canonical_AMOUNT();

    CheckCashBuilder builder{
        accountValue,
        checkIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAmount(amountValue);
    builder.setDeliverMin(deliverMinValue);

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
        auto const& expected = checkIDValue;
        auto const actual = tx.getCheckID();
        expectEqualField(expected, actual, "sfCheckID");
    }

    // Verify optional fields
    {
        auto const& expected = amountValue;
        auto const actualOpt = tx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(tx.hasAmount());
    }

    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = tx.getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
        EXPECT_TRUE(tx.hasDeliverMin());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsCheckCashTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCheckCashFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const checkIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const deliverMinValue = canonical_AMOUNT();

    // Build an initial transaction
    CheckCashBuilder initialBuilder{
        accountValue,
        checkIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAmount(amountValue);
    initialBuilder.setDeliverMin(deliverMinValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    CheckCashBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = checkIDValue;
        auto const actual = rebuiltTx.getCheckID();
        expectEqualField(expected, actual, "sfCheckID");
    }

    // Verify optional fields
    {
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = rebuiltTx.getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsCheckCashTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CheckCash{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsCheckCashTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CheckCashBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsCheckCashTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCheckCashNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const checkIDValue = canonical_UINT256();

    CheckCashBuilder builder{
        accountValue,
        checkIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAmount());
    EXPECT_FALSE(tx.getAmount().has_value());
    EXPECT_FALSE(tx.hasDeliverMin());
    EXPECT_FALSE(tx.getDeliverMin().has_value());
}

}
