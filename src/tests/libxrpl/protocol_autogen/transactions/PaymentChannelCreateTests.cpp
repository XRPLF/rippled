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
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestination(destinationValue);
    builder.setAmount(amountValue);
    builder.setSettleDelay(settleDelayValue);
    builder.setPublicKey(publicKeyValue);
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
    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx.hasDestination());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = tx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(tx.hasAmount());
    }

    {
        auto const& expected = settleDelayValue;
        auto const actualOpt = tx.getSettleDelay();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSettleDelay should be present";
        expectEqualField(expected, *actualOpt, "sfSettleDelay");
        EXPECT_TRUE(tx.hasSettleDelay());
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = tx.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfPublicKey");
        EXPECT_TRUE(tx.hasPublicKey());
    }

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
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestination(destinationValue);
    initialBuilder.setAmount(amountValue);
    initialBuilder.setSettleDelay(settleDelayValue);
    initialBuilder.setPublicKey(publicKeyValue);
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
    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

    {
        auto const& expected = settleDelayValue;
        auto const actualOpt = rebuiltTx.getSettleDelay();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSettleDelay should be present";
        expectEqualField(expected, *actualOpt, "sfSettleDelay");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = rebuiltTx.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfPublicKey");
    }

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

    PaymentChannelCreateBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDestination());
    EXPECT_FALSE(tx.getDestination().has_value());
    EXPECT_FALSE(tx.hasAmount());
    EXPECT_FALSE(tx.getAmount().has_value());
    EXPECT_FALSE(tx.hasSettleDelay());
    EXPECT_FALSE(tx.getSettleDelay().has_value());
    EXPECT_FALSE(tx.hasPublicKey());
    EXPECT_FALSE(tx.getPublicKey().has_value());
    EXPECT_FALSE(tx.hasCancelAfter());
    EXPECT_FALSE(tx.getCancelAfter().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
}

}
