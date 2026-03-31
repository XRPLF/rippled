// Auto-generated unit tests for transaction EscrowFinish


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/EscrowFinish.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsEscrowFinishTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testEscrowFinish"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ownerValue = canonical_ACCOUNT();
    auto const offerSequenceValue = canonical_UINT32();
    auto const fulfillmentValue = canonical_VL();
    auto const conditionValue = canonical_VL();
    auto const credentialIDsValue = canonical_VECTOR256();

    EscrowFinishBuilder builder{
        accountValue,
        ownerValue,
        offerSequenceValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setFulfillment(fulfillmentValue);
    builder.setCondition(conditionValue);
    builder.setCredentialIDs(credentialIDsValue);

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
        auto const& expected = ownerValue;
        auto const actual = tx.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actual = tx.getOfferSequence();
        expectEqualField(expected, actual, "sfOfferSequence");
    }

    // Verify optional fields
    {
        auto const& expected = fulfillmentValue;
        auto const actualOpt = tx.getFulfillment();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFulfillment should be present";
        expectEqualField(expected, *actualOpt, "sfFulfillment");
        EXPECT_TRUE(tx.hasFulfillment());
    }

    {
        auto const& expected = conditionValue;
        auto const actualOpt = tx.getCondition();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCondition should be present";
        expectEqualField(expected, *actualOpt, "sfCondition");
        EXPECT_TRUE(tx.hasCondition());
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = tx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
        EXPECT_TRUE(tx.hasCredentialIDs());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsEscrowFinishTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testEscrowFinishFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ownerValue = canonical_ACCOUNT();
    auto const offerSequenceValue = canonical_UINT32();
    auto const fulfillmentValue = canonical_VL();
    auto const conditionValue = canonical_VL();
    auto const credentialIDsValue = canonical_VECTOR256();

    // Build an initial transaction
    EscrowFinishBuilder initialBuilder{
        accountValue,
        ownerValue,
        offerSequenceValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setFulfillment(fulfillmentValue);
    initialBuilder.setCondition(conditionValue);
    initialBuilder.setCredentialIDs(credentialIDsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    EscrowFinishBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = ownerValue;
        auto const actual = rebuiltTx.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actual = rebuiltTx.getOfferSequence();
        expectEqualField(expected, actual, "sfOfferSequence");
    }

    // Verify optional fields
    {
        auto const& expected = fulfillmentValue;
        auto const actualOpt = rebuiltTx.getFulfillment();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFulfillment should be present";
        expectEqualField(expected, *actualOpt, "sfFulfillment");
    }

    {
        auto const& expected = conditionValue;
        auto const actualOpt = rebuiltTx.getCondition();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCondition should be present";
        expectEqualField(expected, *actualOpt, "sfCondition");
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = rebuiltTx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsEscrowFinishTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(EscrowFinish{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsEscrowFinishTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(EscrowFinishBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsEscrowFinishTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testEscrowFinishNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const ownerValue = canonical_ACCOUNT();
    auto const offerSequenceValue = canonical_UINT32();

    EscrowFinishBuilder builder{
        accountValue,
        ownerValue,
        offerSequenceValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasFulfillment());
    EXPECT_FALSE(tx.getFulfillment().has_value());
    EXPECT_FALSE(tx.hasCondition());
    EXPECT_FALSE(tx.getCondition().has_value());
    EXPECT_FALSE(tx.hasCredentialIDs());
    EXPECT_FALSE(tx.getCredentialIDs().has_value());
}

}
