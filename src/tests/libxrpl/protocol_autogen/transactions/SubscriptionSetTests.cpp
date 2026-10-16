// Auto-generated unit tests for transaction SubscriptionSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/SubscriptionSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsSubscriptionSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSubscriptionSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const frequencyValue = canonical_UINT32();
    auto const startTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const subscriptionIDValue = canonical_UINT256();

    SubscriptionSetBuilder builder{
        accountValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestination(destinationValue);
    builder.setFrequency(frequencyValue);
    builder.setStartTime(startTimeValue);
    builder.setExpiration(expirationValue);
    builder.setDestinationTag(destinationTagValue);
    builder.setSubscriptionID(subscriptionIDValue);

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
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx.hasDestination());
    }

    {
        auto const& expected = frequencyValue;
        auto const actualOpt = tx.getFrequency();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFrequency should be present";
        expectEqualField(expected, *actualOpt, "sfFrequency");
        EXPECT_TRUE(tx.hasFrequency());
    }

    {
        auto const& expected = startTimeValue;
        auto const actualOpt = tx.getStartTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfStartTime should be present";
        expectEqualField(expected, *actualOpt, "sfStartTime");
        EXPECT_TRUE(tx.hasStartTime());
    }

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
        auto const& expected = subscriptionIDValue;
        auto const actualOpt = tx.getSubscriptionID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubscriptionID should be present";
        expectEqualField(expected, *actualOpt, "sfSubscriptionID");
        EXPECT_TRUE(tx.hasSubscriptionID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsSubscriptionSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSubscriptionSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const frequencyValue = canonical_UINT32();
    auto const startTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const destinationTagValue = canonical_UINT32();
    auto const subscriptionIDValue = canonical_UINT256();

    // Build an initial transaction
    SubscriptionSetBuilder initialBuilder{
        accountValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestination(destinationValue);
    initialBuilder.setFrequency(frequencyValue);
    initialBuilder.setStartTime(startTimeValue);
    initialBuilder.setExpiration(expirationValue);
    initialBuilder.setDestinationTag(destinationTagValue);
    initialBuilder.setSubscriptionID(subscriptionIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    SubscriptionSetBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = frequencyValue;
        auto const actualOpt = rebuiltTx.getFrequency();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFrequency should be present";
        expectEqualField(expected, *actualOpt, "sfFrequency");
    }

    {
        auto const& expected = startTimeValue;
        auto const actualOpt = rebuiltTx.getStartTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfStartTime should be present";
        expectEqualField(expected, *actualOpt, "sfStartTime");
    }

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
        auto const& expected = subscriptionIDValue;
        auto const actualOpt = rebuiltTx.getSubscriptionID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubscriptionID should be present";
        expectEqualField(expected, *actualOpt, "sfSubscriptionID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsSubscriptionSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SubscriptionSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsSubscriptionSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SubscriptionSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsSubscriptionSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSubscriptionSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const amountValue = canonical_AMOUNT();

    SubscriptionSetBuilder builder{
        accountValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDestination());
    EXPECT_FALSE(tx.getDestination().has_value());
    EXPECT_FALSE(tx.hasFrequency());
    EXPECT_FALSE(tx.getFrequency().has_value());
    EXPECT_FALSE(tx.hasStartTime());
    EXPECT_FALSE(tx.getStartTime().has_value());
    EXPECT_FALSE(tx.hasExpiration());
    EXPECT_FALSE(tx.getExpiration().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
    EXPECT_FALSE(tx.hasSubscriptionID());
    EXPECT_FALSE(tx.getSubscriptionID().has_value());
}

}
