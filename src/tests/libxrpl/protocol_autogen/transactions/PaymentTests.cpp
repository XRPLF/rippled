// Auto-generated unit tests for transaction Payment
#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPaymentTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPayment"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const pathsValue = canonical_PATHSET();
    auto const invoiceIDValue = canonical_UINT256();
    auto const destinationTagValue = canonical_UINT32();
    auto const deliverMinValue = canonical_AMOUNT();
    auto const credentialIDsValue = canonical_VECTOR256();
    auto const domainIDValue = canonical_UINT256();

    PaymentBuilder builder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setSendMax(sendMaxValue);
    builder.setInvoiceID(invoiceIDValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setDeliverMin(deliverMinValue);
    builder.setCredentialIDs(credentialIDsValue);
    builder.setDomainID(domainIDValue);
    // Set default fields
    builder.setPaths(pathsValue);

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

    // Verify required and default fields
    {
        auto const& expected = destinationValue;
        auto const actual = tx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = pathsValue;
        auto const actual = tx.getPaths();
        expectEqualField(expected, actual, "sfPaths");
    }

    // Verify optional fields
    {
        auto const& expected = sendMaxValue;
        auto const actualOpt = tx.getSendMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSendMax should be present";
        expectEqualField(expected, *actualOpt, "sfSendMax");
        EXPECT_TRUE(tx.hasSendMax());
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = tx.getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInvoiceID should be present";
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
        EXPECT_TRUE(tx.hasInvoiceID());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = tx.getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
        EXPECT_TRUE(tx.hasDeliverMin());
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = tx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
        EXPECT_TRUE(tx.hasCredentialIDs());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPaymentTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPaymentFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const pathsValue = canonical_PATHSET();
    auto const invoiceIDValue = canonical_UINT256();
    auto const destinationTagValue = canonical_UINT32();
    auto const deliverMinValue = canonical_AMOUNT();
    auto const credentialIDsValue = canonical_VECTOR256();
    auto const domainIDValue = canonical_UINT256();

    // Build an initial transaction
    PaymentBuilder initialBuilder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setSendMax(sendMaxValue);
    initialBuilder.setInvoiceID(invoiceIDValue);
    initialBuilder.setDestinationTag(destinationTagValue);
    initialBuilder.setDeliverMin(deliverMinValue);
    initialBuilder.setCredentialIDs(credentialIDsValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setPaths(pathsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PaymentBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required and default fields
    {
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = pathsValue;
        auto const actual = rebuiltTx.getPaths();
        expectEqualField(expected, actual, "sfPaths");
    }

    // Verify optional fields
    {
        auto const& expected = sendMaxValue;
        auto const actualOpt = rebuiltTx.getSendMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSendMax should be present";
        expectEqualField(expected, *actualOpt, "sfSendMax");
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = rebuiltTx.getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInvoiceID should be present";
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = rebuiltTx.getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = rebuiltTx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPaymentTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(Payment{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPaymentTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PaymentBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPaymentTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPaymentNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();

    PaymentBuilder builder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasSendMax());
    EXPECT_FALSE(tx.getSendMax().has_value());
    EXPECT_FALSE(tx.hasInvoiceID());
    EXPECT_FALSE(tx.getInvoiceID().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
    EXPECT_FALSE(tx.hasDeliverMin());
    EXPECT_FALSE(tx.getDeliverMin().has_value());
    EXPECT_FALSE(tx.hasCredentialIDs());
    EXPECT_FALSE(tx.getCredentialIDs().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
}

// 6) Default fields return the type default when unset, and the assigned value
// after being set.
TEST(TransactionsPaymentTests, DefaultFieldsRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPaymentDefault"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 4;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();

    // Unset: default fields return the type default.
    PaymentBuilder defaultBuilder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };
    auto const defaultTx = defaultBuilder.build(publicKey, secretKey);
    {
        auto const expected = STPathSet{};
        expectEqualField(expected, defaultTx.getPaths(), "sfPaths");
    }

    // Set: default fields return the assigned value.
    PaymentBuilder setBuilder{
        accountValue,
        destinationValue,
        amountValue,
        sequenceValue,
        feeValue
    };
    setBuilder.setPaths(canonical_PATHSET());
    auto const setTx = setBuilder.build(publicKey, secretKey);
    {
        auto const expected = canonical_PATHSET();
        expectEqualField(expected, setTx.getPaths(), "sfPaths");
    }
}

}
