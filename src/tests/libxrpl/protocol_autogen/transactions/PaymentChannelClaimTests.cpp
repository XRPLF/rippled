// Auto-generated unit tests for transaction PaymentChannelClaim


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/PaymentChannelClaim.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPaymentChannelClaimTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentChannelClaim"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const channelValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const signatureValue = canonical_VL();
    auto const publicKeyValue = canonical_VL();
    auto const credentialIDsValue = canonical_VECTOR256();

    PaymentChannelClaimBuilder builder{
        accountValue,
        channelValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAmount(amountValue);
    builder.setBalance(balanceValue);
    builder.setSignature(signatureValue);
    builder.setPublicKey(publicKeyValue);
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
        auto const& expected = channelValue;
        auto const actual = tx.getChannel();
        expectEqualField(expected, actual, "sfChannel");
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
        auto const& expected = balanceValue;
        auto const actualOpt = tx.getBalance();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBalance should be present";
        expectEqualField(expected, *actualOpt, "sfBalance");
        EXPECT_TRUE(tx.hasBalance());
    }

    {
        auto const& expected = signatureValue;
        auto const actualOpt = tx.getSignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignature should be present";
        expectEqualField(expected, *actualOpt, "sfSignature");
        EXPECT_TRUE(tx.hasSignature());
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = tx.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfPublicKey");
        EXPECT_TRUE(tx.hasPublicKey());
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
TEST(TransactionsPaymentChannelClaimTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentChannelClaimFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const channelValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const balanceValue = canonical_AMOUNT();
    auto const signatureValue = canonical_VL();
    auto const publicKeyValue = canonical_VL();
    auto const credentialIDsValue = canonical_VECTOR256();

    // Build an initial transaction
    PaymentChannelClaimBuilder initialBuilder{
        accountValue,
        channelValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAmount(amountValue);
    initialBuilder.setBalance(balanceValue);
    initialBuilder.setSignature(signatureValue);
    initialBuilder.setPublicKey(publicKeyValue);
    initialBuilder.setCredentialIDs(credentialIDsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PaymentChannelClaimBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = channelValue;
        auto const actual = rebuiltTx.getChannel();
        expectEqualField(expected, actual, "sfChannel");
    }

    // Verify optional fields
    {
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

    {
        auto const& expected = balanceValue;
        auto const actualOpt = rebuiltTx.getBalance();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBalance should be present";
        expectEqualField(expected, *actualOpt, "sfBalance");
    }

    {
        auto const& expected = signatureValue;
        auto const actualOpt = rebuiltTx.getSignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignature should be present";
        expectEqualField(expected, *actualOpt, "sfSignature");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = rebuiltTx.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfPublicKey");
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = rebuiltTx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPaymentChannelClaimTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PaymentChannelClaim{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPaymentChannelClaimTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PaymentChannelClaimBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPaymentChannelClaimTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testPaymentChannelClaimNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const channelValue = canonical_UINT256();

    PaymentChannelClaimBuilder builder{
        accountValue,
        channelValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAmount());
    EXPECT_FALSE(tx.getAmount().has_value());
    EXPECT_FALSE(tx.hasBalance());
    EXPECT_FALSE(tx.getBalance().has_value());
    EXPECT_FALSE(tx.hasSignature());
    EXPECT_FALSE(tx.getSignature().has_value());
    EXPECT_FALSE(tx.hasPublicKey());
    EXPECT_FALSE(tx.getPublicKey().has_value());
    EXPECT_FALSE(tx.hasCredentialIDs());
    EXPECT_FALSE(tx.getCredentialIDs().has_value());
}

}
