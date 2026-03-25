// Auto-generated unit tests for transaction MPTokenIssuanceSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/MPTokenIssuanceSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsMPTokenIssuanceSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenIssuanceSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderValue = canonical_ACCOUNT();
    auto const domainIDValue = canonical_UINT256();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const transferFeeValue = canonical_UINT16();
    auto const mutableFlagsValue = canonical_UINT32();

    MPTokenIssuanceSetBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setHolder(holderValue);
    builder.setDomainID(domainIDValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);
    builder.setTransferFee(transferFeeValue);
    builder.setMutableFlags(mutableFlagsValue);

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

    // Verify optional fields
    {
        auto const& expected = holderValue;
        auto const actualOpt = tx.getHolder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolder should be present";
        expectEqualField(expected, *actualOpt, "sfHolder");
        EXPECT_TRUE(tx.hasHolder());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = tx.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenMetadata should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
        EXPECT_TRUE(tx.hasMPTokenMetadata());
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = tx.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(tx.hasTransferFee());
    }

    {
        auto const& expected = mutableFlagsValue;
        auto const actualOpt = tx.getMutableFlags();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMutableFlags should be present";
        expectEqualField(expected, *actualOpt, "sfMutableFlags");
        EXPECT_TRUE(tx.hasMutableFlags());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsMPTokenIssuanceSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenIssuanceSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderValue = canonical_ACCOUNT();
    auto const domainIDValue = canonical_UINT256();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const transferFeeValue = canonical_UINT16();
    auto const mutableFlagsValue = canonical_UINT32();

    // Build an initial transaction
    MPTokenIssuanceSetBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setHolder(holderValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setMPTokenMetadata(mPTokenMetadataValue);
    initialBuilder.setTransferFee(transferFeeValue);
    initialBuilder.setMutableFlags(mutableFlagsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    MPTokenIssuanceSetBuilder builderFromTx{initialTx.getSTTx()};

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

    // Verify optional fields
    {
        auto const& expected = holderValue;
        auto const actualOpt = rebuiltTx.getHolder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolder should be present";
        expectEqualField(expected, *actualOpt, "sfHolder");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = rebuiltTx.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenMetadata should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = rebuiltTx.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
    }

    {
        auto const& expected = mutableFlagsValue;
        auto const actualOpt = rebuiltTx.getMutableFlags();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMutableFlags should be present";
        expectEqualField(expected, *actualOpt, "sfMutableFlags");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsMPTokenIssuanceSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(MPTokenIssuanceSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsMPTokenIssuanceSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(MPTokenIssuanceSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsMPTokenIssuanceSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenIssuanceSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();

    MPTokenIssuanceSetBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasHolder());
    EXPECT_FALSE(tx.getHolder().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
    EXPECT_FALSE(tx.hasMPTokenMetadata());
    EXPECT_FALSE(tx.getMPTokenMetadata().has_value());
    EXPECT_FALSE(tx.hasTransferFee());
    EXPECT_FALSE(tx.getTransferFee().has_value());
    EXPECT_FALSE(tx.hasMutableFlags());
    EXPECT_FALSE(tx.getMutableFlags().has_value());
}

}
