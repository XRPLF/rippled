// Auto-generated unit tests for transaction ConfidentialMPTConvert


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ConfidentialMPTConvert.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsConfidentialMPTConvertTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTConvert"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const holderEncryptionKeyValue = canonical_VL();
    auto const holderEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();
    auto const zKProofValue = canonical_VL();

    ConfidentialMPTConvertBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        mPTAmountValue,
        holderEncryptedAmountValue,
        issuerEncryptedAmountValue,
        blindingFactorValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setHolderEncryptionKey(holderEncryptionKeyValue);
    builder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);
    builder.setZKProof(zKProofValue);

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
        auto const& expected = mPTAmountValue;
        auto const actual = tx.getMPTAmount();
        expectEqualField(expected, actual, "sfMPTAmount");
    }

    {
        auto const& expected = holderEncryptedAmountValue;
        auto const actual = tx.getHolderEncryptedAmount();
        expectEqualField(expected, actual, "sfHolderEncryptedAmount");
    }

    {
        auto const& expected = issuerEncryptedAmountValue;
        auto const actual = tx.getIssuerEncryptedAmount();
        expectEqualField(expected, actual, "sfIssuerEncryptedAmount");
    }

    {
        auto const& expected = blindingFactorValue;
        auto const actual = tx.getBlindingFactor();
        expectEqualField(expected, actual, "sfBlindingFactor");
    }

    // Verify optional fields
    {
        auto const& expected = holderEncryptionKeyValue;
        auto const actualOpt = tx.getHolderEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolderEncryptionKey should be present";
        expectEqualField(expected, *actualOpt, "sfHolderEncryptionKey");
        EXPECT_TRUE(tx.hasHolderEncryptionKey());
    }

    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = tx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
        EXPECT_TRUE(tx.hasAuditorEncryptedAmount());
    }

    {
        auto const& expected = zKProofValue;
        auto const actualOpt = tx.getZKProof();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfZKProof should be present";
        expectEqualField(expected, *actualOpt, "sfZKProof");
        EXPECT_TRUE(tx.hasZKProof());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsConfidentialMPTConvertTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTConvertFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const holderEncryptionKeyValue = canonical_VL();
    auto const holderEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();
    auto const zKProofValue = canonical_VL();

    // Build an initial transaction
    ConfidentialMPTConvertBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        mPTAmountValue,
        holderEncryptedAmountValue,
        issuerEncryptedAmountValue,
        blindingFactorValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setHolderEncryptionKey(holderEncryptionKeyValue);
    initialBuilder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);
    initialBuilder.setZKProof(zKProofValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ConfidentialMPTConvertBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = mPTAmountValue;
        auto const actual = rebuiltTx.getMPTAmount();
        expectEqualField(expected, actual, "sfMPTAmount");
    }

    {
        auto const& expected = holderEncryptedAmountValue;
        auto const actual = rebuiltTx.getHolderEncryptedAmount();
        expectEqualField(expected, actual, "sfHolderEncryptedAmount");
    }

    {
        auto const& expected = issuerEncryptedAmountValue;
        auto const actual = rebuiltTx.getIssuerEncryptedAmount();
        expectEqualField(expected, actual, "sfIssuerEncryptedAmount");
    }

    {
        auto const& expected = blindingFactorValue;
        auto const actual = rebuiltTx.getBlindingFactor();
        expectEqualField(expected, actual, "sfBlindingFactor");
    }

    // Verify optional fields
    {
        auto const& expected = holderEncryptionKeyValue;
        auto const actualOpt = rebuiltTx.getHolderEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolderEncryptionKey should be present";
        expectEqualField(expected, *actualOpt, "sfHolderEncryptionKey");
    }

    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = rebuiltTx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
    }

    {
        auto const& expected = zKProofValue;
        auto const actualOpt = rebuiltTx.getZKProof();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfZKProof should be present";
        expectEqualField(expected, *actualOpt, "sfZKProof");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTConvertTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTConvert{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTConvertTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTConvertBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsConfidentialMPTConvertTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTConvertNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const holderEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();

    ConfidentialMPTConvertBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        mPTAmountValue,
        holderEncryptedAmountValue,
        issuerEncryptedAmountValue,
        blindingFactorValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasHolderEncryptionKey());
    EXPECT_FALSE(tx.getHolderEncryptionKey().has_value());
    EXPECT_FALSE(tx.hasAuditorEncryptedAmount());
    EXPECT_FALSE(tx.getAuditorEncryptedAmount().has_value());
    EXPECT_FALSE(tx.hasZKProof());
    EXPECT_FALSE(tx.getZKProof().has_value());
}

}
