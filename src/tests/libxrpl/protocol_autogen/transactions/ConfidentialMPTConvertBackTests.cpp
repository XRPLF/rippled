// Auto-generated unit tests for transaction ConfidentialMPTConvertBack


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ConfidentialMPTConvertBack.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsConfidentialMPTConvertBackTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTConvertBack"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const holderEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();
    auto const zKProofValue = canonical_VL();
    auto const balanceCommitmentValue = canonical_VL();

    ConfidentialMPTConvertBackBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        mPTAmountValue,
        holderEncryptedAmountValue,
        issuerEncryptedAmountValue,
        blindingFactorValue,
        zKProofValue,
        balanceCommitmentValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);

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

    {
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    {
        auto const& expected = balanceCommitmentValue;
        auto const actual = tx.getBalanceCommitment();
        expectEqualField(expected, actual, "sfBalanceCommitment");
    }

    // Verify optional fields
    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = tx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
        EXPECT_TRUE(tx.hasAuditorEncryptedAmount());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsConfidentialMPTConvertBackTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTConvertBackFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const mPTAmountValue = canonical_UINT64();
    auto const holderEncryptedAmountValue = canonical_VL();
    auto const issuerEncryptedAmountValue = canonical_VL();
    auto const auditorEncryptedAmountValue = canonical_VL();
    auto const blindingFactorValue = canonical_UINT256();
    auto const zKProofValue = canonical_VL();
    auto const balanceCommitmentValue = canonical_VL();

    // Build an initial transaction
    ConfidentialMPTConvertBackBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        mPTAmountValue,
        holderEncryptedAmountValue,
        issuerEncryptedAmountValue,
        blindingFactorValue,
        zKProofValue,
        balanceCommitmentValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAuditorEncryptedAmount(auditorEncryptedAmountValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ConfidentialMPTConvertBackBuilder builderFromTx{initialTx.getSTTx()};

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

    {
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    {
        auto const& expected = balanceCommitmentValue;
        auto const actual = rebuiltTx.getBalanceCommitment();
        expectEqualField(expected, actual, "sfBalanceCommitment");
    }

    // Verify optional fields
    {
        auto const& expected = auditorEncryptedAmountValue;
        auto const actualOpt = rebuiltTx.getAuditorEncryptedAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptedAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptedAmount");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTConvertBackTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTConvertBack{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTConvertBackTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTConvertBackBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsConfidentialMPTConvertBackTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTConvertBackNullopt"));

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
    auto const zKProofValue = canonical_VL();
    auto const balanceCommitmentValue = canonical_VL();

    ConfidentialMPTConvertBackBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        mPTAmountValue,
        holderEncryptedAmountValue,
        issuerEncryptedAmountValue,
        blindingFactorValue,
        zKProofValue,
        balanceCommitmentValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAuditorEncryptedAmount());
    EXPECT_FALSE(tx.getAuditorEncryptedAmount().has_value());
}

}
