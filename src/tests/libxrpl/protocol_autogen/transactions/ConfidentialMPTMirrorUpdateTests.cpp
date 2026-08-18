// Auto-generated unit tests for transaction ConfidentialMPTMirrorUpdate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ConfidentialMPTMirrorUpdate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsConfidentialMPTMirrorUpdateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTMirrorUpdate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderValue = canonical_ACCOUNT();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const previousIssuerEncryptionKeyValue = canonical_VL();
    auto const zKProofValue = canonical_VL();

    ConfidentialMPTMirrorUpdateBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setHolder(holderValue);
    builder.setIssuerEncryptedAmount(issuerEncryptedAmountValue);
    builder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);
    builder.setPreviousIssuerEncryptionKey(previousIssuerEncryptionKeyValue);

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
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
    {
        auto const& expected = holderValue;
        auto const actualOpt = tx.getHolder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolder should be present";
        expectEqualField(expected, *actualOpt, "sfHolder");
        EXPECT_TRUE(tx.hasHolder());
    }

    {
        auto const& expected = issuerEncryptedAmountValue;
        auto const actualOpt = tx.getIssuerEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIssuerEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfIssuerEncryptedAmount");
        EXPECT_TRUE(tx.hasIssuerEncryptedAmount());
    }

    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = tx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
        EXPECT_TRUE(tx.hasAuditorEncryptedAmount());
    }

    {
        auto const& expected = previousIssuerEncryptionKeyValue;
        auto const actualOpt = tx.getPreviousIssuerEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPreviousIssuerEncryptionKey should be present";
        expectEqualField(expected, *actualOpt, "sfPreviousIssuerEncryptionKey");
        EXPECT_TRUE(tx.hasPreviousIssuerEncryptionKey());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsConfidentialMPTMirrorUpdateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTMirrorUpdateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderValue = canonical_ACCOUNT();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const previousIssuerEncryptionKeyValue = canonical_VL();
    auto const zKProofValue = canonical_VL();

    // Build an initial transaction
    ConfidentialMPTMirrorUpdateBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setHolder(holderValue);
    initialBuilder.setIssuerEncryptedAmount(issuerEncryptedAmountValue);
    initialBuilder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);
    initialBuilder.setPreviousIssuerEncryptionKey(previousIssuerEncryptionKeyValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ConfidentialMPTMirrorUpdateBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
    {
        auto const& expected = holderValue;
        auto const actualOpt = rebuiltTx.getHolder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolder should be present";
        expectEqualField(expected, *actualOpt, "sfHolder");
    }

    {
        auto const& expected = issuerEncryptedAmountValue;
        auto const actualOpt = rebuiltTx.getIssuerEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIssuerEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfIssuerEncryptedAmount");
    }

    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = rebuiltTx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
    }

    {
        auto const& expected = previousIssuerEncryptionKeyValue;
        auto const actualOpt = rebuiltTx.getPreviousIssuerEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPreviousIssuerEncryptionKey should be present";
        expectEqualField(expected, *actualOpt, "sfPreviousIssuerEncryptionKey");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTMirrorUpdateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTMirrorUpdate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTMirrorUpdateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTMirrorUpdateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsConfidentialMPTMirrorUpdateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTMirrorUpdateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const zKProofValue = canonical_VL();

    ConfidentialMPTMirrorUpdateBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasHolder());
    EXPECT_FALSE(tx.getHolder().has_value());
    EXPECT_FALSE(tx.hasIssuerEncryptedAmount());
    EXPECT_FALSE(tx.getIssuerEncryptedAmount().has_value());
    EXPECT_FALSE(tx.hasAuditorEncryptedAmount());
    EXPECT_FALSE(tx.getAuditorEncryptedAmount().has_value());
    EXPECT_FALSE(tx.hasPreviousIssuerEncryptionKey());
    EXPECT_FALSE(tx.getPreviousIssuerEncryptionKey().has_value());
}

}
