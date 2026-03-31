// Auto-generated unit tests for transaction PaymentChannelCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/PaymentChannelCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPaymentChannelCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentChannelCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const settleDelayValue = canonical_UINT32();
    auto const publicKeyValue = canonical_VL();
    auto const cancelAfterValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();

    PaymentChannelCreateBuilder builder{
        accountValue,
        destinationValue,
        amountValue,
        settleDelayValue,
        publicKeyValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCancelAfter(cancelAfterValue);
    builder.setDestinationTag(destinationTagValue);

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
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = settleDelayValue;
        auto const actual = tx.getSettleDelay();
        expectEqualField(expected, actual, "sfSettleDelay");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actual = tx.getPublicKey();
        expectEqualField(expected, actual, "sfPublicKey");
    }

    // Verify optional fields
    {
        auto const& expected = cancelAfterValue;
        auto const actualOpt = tx.getCancelAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCancelAfter should be present";
        expectEqualField(expected, *actualOpt, "sfCancelAfter");
        EXPECT_TRUE(tx.hasCancelAfter());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPaymentChannelCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentChannelCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const settleDelayValue = canonical_UINT32();
    auto const publicKeyValue = canonical_VL();
    auto const cancelAfterValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();

    // Build an initial transaction
    PaymentChannelCreateBuilder initialBuilder{
        accountValue,
        destinationValue,
        amountValue,
        settleDelayValue,
        publicKeyValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCancelAfter(cancelAfterValue);
    initialBuilder.setDestinationTag(destinationTagValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PaymentChannelCreateBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = settleDelayValue;
        auto const actual = rebuiltTx.getSettleDelay();
        expectEqualField(expected, actual, "sfSettleDelay");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actual = rebuiltTx.getPublicKey();
        expectEqualField(expected, actual, "sfPublicKey");
    }

    // Verify optional fields
    {
        auto const& expected = cancelAfterValue;
        auto const actualOpt = rebuiltTx.getCancelAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCancelAfter should be present";
        expectEqualField(expected, *actualOpt, "sfCancelAfter");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPaymentChannelCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PaymentChannelCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPaymentChannelCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PaymentChannelCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPaymentChannelCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentChannelCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const settleDelayValue = canonical_UINT32();
    auto const publicKeyValue = canonical_VL();

    PaymentChannelCreateBuilder builder{
        accountValue,
        destinationValue,
        amountValue,
        settleDelayValue,
        publicKeyValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCancelAfter());
    EXPECT_FALSE(tx.getCancelAfter().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
}

}
