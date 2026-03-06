// Auto-generated unit tests for transaction Payment

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPaymentTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPayment"));

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
    builder.setPaths(pathsValue);
    builder.setInvoiceID(invoiceIDValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setDeliverMin(deliverMinValue);
    builder.setCredentialIDs(credentialIDsValue);
    builder.setDomainID(domainIDValue);

    std::string reason;
    EXPECT_TRUE(builder.validate(reason)) << reason;

    auto tx = builder.build(publicKey, secretKey);

    EXPECT_TRUE(tx->validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx->getSigningPubKey().empty());
    EXPECT_TRUE(tx->hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx->getAccount(), accountValue);
    EXPECT_EQ(tx->getSequence(), sequenceValue);
    EXPECT_EQ(tx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = destinationValue;
        auto const actual = tx->getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = sendMaxValue;
        auto const actualOpt = tx->getSendMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSendMax should be present";
        expectEqualField(expected, *actualOpt, "sfSendMax");
        EXPECT_TRUE(tx->hasSendMax());
    }

    {
        auto const& expected = pathsValue;
        auto const actualOpt = tx->getPaths();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPaths should be present";
        expectEqualField(expected, *actualOpt, "sfPaths");
        EXPECT_TRUE(tx->hasPaths());
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = tx->getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInvoiceID should be present";
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
        EXPECT_TRUE(tx->hasInvoiceID());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx->getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx->hasDestinationTag());
    }

    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = tx->getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
        EXPECT_TRUE(tx->hasDeliverMin());
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = tx->getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
        EXPECT_TRUE(tx->hasCredentialIDs());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx->getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx->hasDomainID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPaymentTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentFromTx"));

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
    initialBuilder.setPaths(pathsValue);
    initialBuilder.setInvoiceID(invoiceIDValue);
    initialBuilder.setDestinationTag(destinationTagValue);
    initialBuilder.setDeliverMin(deliverMinValue);
    initialBuilder.setCredentialIDs(credentialIDsValue);
    initialBuilder.setDomainID(domainIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PaymentBuilder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx->getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx->getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = sendMaxValue;
        auto const actualOpt = rebuiltTx->getSendMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSendMax should be present";
        expectEqualField(expected, *actualOpt, "sfSendMax");
    }

    {
        auto const& expected = pathsValue;
        auto const actualOpt = rebuiltTx->getPaths();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPaths should be present";
        expectEqualField(expected, *actualOpt, "sfPaths");
    }

    {
        auto const& expected = invoiceIDValue;
        auto const actualOpt = rebuiltTx->getInvoiceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInvoiceID should be present";
        expectEqualField(expected, *actualOpt, "sfInvoiceID");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx->getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = rebuiltTx->getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = rebuiltTx->getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx->getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

}

}
