// Auto-generated unit tests for transaction CheckCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/CheckCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsCheckCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCheckCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const expirationValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const invoiceIDValue = canonical_UINT256();

    CheckCreateBuilder builder{
        accountValue,
        destinationValue,
        sendMaxValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setExpiration(expirationValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setInvoiceID(invoiceIDValue);

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
        auto const& expected = destinationValue;
        auto const actual = tx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = sendMaxValue;
        auto const actual = tx.getSendMax();
        expectEqualField(expected, actual, "sfSendMax");
    }

    // Verify optional fields
    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx.hasExpiration());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = tx.getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInvoiceID should be present";
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
        EXPECT_TRUE(tx.hasInvoiceID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsCheckCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCheckCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const expirationValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const invoiceIDValue = canonical_UINT256();

    // Build an initial transaction
    CheckCreateBuilder initialBuilder{
        accountValue,
        destinationValue,
        sendMaxValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setExpiration(expirationValue);
    initialBuilder.setDestinationTag(destinationTagValue);
    initialBuilder.setInvoiceID(invoiceIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    CheckCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = sendMaxValue;
        auto const actual = rebuiltTx.getSendMax();
        expectEqualField(expected, actual, "sfSendMax");
    }

    // Verify optional fields
    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = rebuiltTx.getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInvoiceID should be present";
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsCheckCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CheckCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsCheckCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CheckCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsCheckCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCheckCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const sendMaxValue = canonical_AMOUNT();

    CheckCreateBuilder builder{
        accountValue,
        destinationValue,
        sendMaxValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasExpiration());
    EXPECT_FALSE(tx.getExpiration().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
    EXPECT_FALSE(tx.hasInvoiceID());
    EXPECT_FALSE(tx.getInvoiceID().has_value());
}

}
