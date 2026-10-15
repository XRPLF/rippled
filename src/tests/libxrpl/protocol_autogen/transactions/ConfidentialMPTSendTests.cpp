// Auto-generated unit tests for transaction ConfidentialMPTSend


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ConfidentialMPTSend.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsConfidentialMPTSendTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTSend"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const senderEncryptedAmountValue = canonical_VL();
    auto const destinationEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const zKProofValue = canonical_VL();
    auto const amountCommitmentValue = canonical_VL();
    auto const balanceCommitmentValue = canonical_VL();
    auto const credentialIDsValue = canonical_VECTOR256();

    ConfidentialMPTSendBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        destinationValue,
        senderEncryptedAmountValue,
        destinationEncryptedAmountValue,
        issuerEncryptedAmountValue,
        zKProofValue,
        amountCommitmentValue,
        balanceCommitmentValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestinationTag(destinationTagValue);
    builder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);
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
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = tx.getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = destinationValue;
        auto const actual = tx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = senderEncryptedAmountValue;
        auto const actual = tx.getSenderEncryptedAmount();
        expectEqualField(expected, actual, "sfSenderEncryptedAmount");
    }

    {
        auto const& expected = destinationEncryptedAmountValue;
        auto const actual = tx.getDestinationEncryptedAmount();
        expectEqualField(expected, actual, "sfDestinationEncryptedAmount");
    }

    {
        auto const& expected = issuerEncryptedAmountValue;
        auto const actual = tx.getIssuerEncryptedAmount();
        expectEqualField(expected, actual, "sfIssuerEncryptedAmount");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    {
        auto const& expected = amountCommitmentValue;
        auto const actual = tx.getAmountCommitment();
        expectEqualField(expected, actual, "sfAmountCommitment");
    }

    {
        auto const& expected = balanceCommitmentValue;
        auto const actual = tx.getBalanceCommitment();
        expectEqualField(expected, actual, "sfBalanceCommitment");
    }

    // Verify optional fields
    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = tx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
        EXPECT_TRUE(tx.hasAuditorEncryptedAmount());
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
TEST(TransactionsConfidentialMPTSendTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTSendFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const senderEncryptedAmountValue = canonical_VL();
    auto const destinationEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const zKProofValue = canonical_VL();
    auto const amountCommitmentValue = canonical_VL();
    auto const balanceCommitmentValue = canonical_VL();
    auto const credentialIDsValue = canonical_VECTOR256();

    // Build an initial transaction
    ConfidentialMPTSendBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        destinationValue,
        senderEncryptedAmountValue,
        destinationEncryptedAmountValue,
        issuerEncryptedAmountValue,
        zKProofValue,
        amountCommitmentValue,
        balanceCommitmentValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestinationTag(destinationTagValue);
    initialBuilder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);
    initialBuilder.setCredentialIDs(credentialIDsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ConfidentialMPTSendBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = rebuiltTx.getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    {
        auto const& expected = senderEncryptedAmountValue;
        auto const actual = rebuiltTx.getSenderEncryptedAmount();
        expectEqualField(expected, actual, "sfSenderEncryptedAmount");
    }

    {
        auto const& expected = destinationEncryptedAmountValue;
        auto const actual = rebuiltTx.getDestinationEncryptedAmount();
        expectEqualField(expected, actual, "sfDestinationEncryptedAmount");
    }

    {
        auto const& expected = issuerEncryptedAmountValue;
        auto const actual = rebuiltTx.getIssuerEncryptedAmount();
        expectEqualField(expected, actual, "sfIssuerEncryptedAmount");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    {
        auto const& expected = amountCommitmentValue;
        auto const actual = rebuiltTx.getAmountCommitment();
        expectEqualField(expected, actual, "sfAmountCommitment");
    }

    {
        auto const& expected = balanceCommitmentValue;
        auto const actual = rebuiltTx.getBalanceCommitment();
        expectEqualField(expected, actual, "sfBalanceCommitment");
    }

    // Verify optional fields
    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = rebuiltTx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
    }

    {
        auto const& expected = credentialIDsValue;
        auto const actualOpt = rebuiltTx.getCredentialIDs();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCredentialIDs should be present";
        expectEqualField(expected, *actualOpt, "sfCredentialIDs");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTSendTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTSend{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTSendTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTSendBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsConfidentialMPTSendTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTSendNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const destinationValue = canonical_ACCOUNT();
    auto const senderEncryptedAmountValue = canonical_VL();
    auto const destinationEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const zKProofValue = canonical_VL();
    auto const amountCommitmentValue = canonical_VL();
    auto const balanceCommitmentValue = canonical_VL();

    ConfidentialMPTSendBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        destinationValue,
        senderEncryptedAmountValue,
        destinationEncryptedAmountValue,
        issuerEncryptedAmountValue,
        zKProofValue,
        amountCommitmentValue,
        balanceCommitmentValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
    EXPECT_FALSE(tx.hasAuditorEncryptedAmount());
    EXPECT_FALSE(tx.getAuditorEncryptedAmount().has_value());
    EXPECT_FALSE(tx.hasCredentialIDs());
    EXPECT_FALSE(tx.getCredentialIDs().has_value());
}

}
