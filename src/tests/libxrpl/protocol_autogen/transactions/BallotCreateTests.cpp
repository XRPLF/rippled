// Auto-generated unit tests for transaction BallotCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/BallotCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsBallotCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const domainIDValue = canonical_UINT256();
    auto const digestValue = canonical_UINT256();
    auto const uRIValue = canonical_VL();
    auto const optionCountValue = canonical_UINT8();
    auto const tallyPublicKeyValue = canonical_VL();
    auto const auditorEncryptionKeyValue = canonical_VL();
    auto const openTimeValue = canonical_UINT32();
    auto const closeTimeValue = canonical_UINT32();
    auto const zKProofValue = canonical_VL();

    BallotCreateBuilder builder{
        accountValue,
        digestValue,
        optionCountValue,
        tallyPublicKeyValue,
        openTimeValue,
        closeTimeValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    builder.setDomainID(domainIDValue);
    builder.setURI(uRIValue);
    builder.setAuditorEncryptionKey(auditorEncryptionKeyValue);

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
        auto const& expected = digestValue;
        auto const actual = tx.getDigest();
        expectEqualField(expected, actual, "sfDigest");
    }

    {
        auto const& expected = optionCountValue;
        auto const actual = tx.getOptionCount();
        expectEqualField(expected, actual, "sfOptionCount");
    }

    {
        auto const& expected = tallyPublicKeyValue;
        auto const actual = tx.getTallyPublicKey();
        expectEqualField(expected, actual, "sfTallyPublicKey");
    }

    {
        auto const& expected = openTimeValue;
        auto const actual = tx.getOpenTime();
        expectEqualField(expected, actual, "sfOpenTime");
    }

    {
        auto const& expected = closeTimeValue;
        auto const actual = tx.getCloseTime();
        expectEqualField(expected, actual, "sfCloseTime");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = tx.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenIssuanceID should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
        EXPECT_TRUE(tx.hasMPTokenIssuanceID());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = tx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(tx.hasURI());
    }

    {
        auto const& expected = auditorEncryptionKeyValue;
        auto const actualOpt = tx.getAuditorEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptionKey should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptionKey");
        EXPECT_TRUE(tx.hasAuditorEncryptionKey());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsBallotCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const domainIDValue = canonical_UINT256();
    auto const digestValue = canonical_UINT256();
    auto const uRIValue = canonical_VL();
    auto const optionCountValue = canonical_UINT8();
    auto const tallyPublicKeyValue = canonical_VL();
    auto const auditorEncryptionKeyValue = canonical_VL();
    auto const openTimeValue = canonical_UINT32();
    auto const closeTimeValue = canonical_UINT32();
    auto const zKProofValue = canonical_VL();

    // Build an initial transaction
    BallotCreateBuilder initialBuilder{
        accountValue,
        digestValue,
        optionCountValue,
        tallyPublicKeyValue,
        openTimeValue,
        closeTimeValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setURI(uRIValue);
    initialBuilder.setAuditorEncryptionKey(auditorEncryptionKeyValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    BallotCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = digestValue;
        auto const actual = rebuiltTx.getDigest();
        expectEqualField(expected, actual, "sfDigest");
    }

    {
        auto const& expected = optionCountValue;
        auto const actual = rebuiltTx.getOptionCount();
        expectEqualField(expected, actual, "sfOptionCount");
    }

    {
        auto const& expected = tallyPublicKeyValue;
        auto const actual = rebuiltTx.getTallyPublicKey();
        expectEqualField(expected, actual, "sfTallyPublicKey");
    }

    {
        auto const& expected = openTimeValue;
        auto const actual = rebuiltTx.getOpenTime();
        expectEqualField(expected, actual, "sfOpenTime");
    }

    {
        auto const& expected = closeTimeValue;
        auto const actual = rebuiltTx.getCloseTime();
        expectEqualField(expected, actual, "sfCloseTime");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = rebuiltTx.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenIssuanceID should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = rebuiltTx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
    }

    {
        auto const& expected = auditorEncryptionKeyValue;
        auto const actualOpt = rebuiltTx.getAuditorEncryptionKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuditorEncryptionKey should be present";
        expectEqualField(expected, *actualOpt, "sfAuditorEncryptionKey");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsBallotCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BallotCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsBallotCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BallotCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsBallotCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBallotCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const digestValue = canonical_UINT256();
    auto const optionCountValue = canonical_UINT8();
    auto const tallyPublicKeyValue = canonical_VL();
    auto const openTimeValue = canonical_UINT32();
    auto const closeTimeValue = canonical_UINT32();
    auto const zKProofValue = canonical_VL();

    BallotCreateBuilder builder{
        accountValue,
        digestValue,
        optionCountValue,
        tallyPublicKeyValue,
        openTimeValue,
        closeTimeValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasMPTokenIssuanceID());
    EXPECT_FALSE(tx.getMPTokenIssuanceID().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
    EXPECT_FALSE(tx.hasURI());
    EXPECT_FALSE(tx.getURI().has_value());
    EXPECT_FALSE(tx.hasAuditorEncryptionKey());
    EXPECT_FALSE(tx.getAuditorEncryptionKey().has_value());
}

}
