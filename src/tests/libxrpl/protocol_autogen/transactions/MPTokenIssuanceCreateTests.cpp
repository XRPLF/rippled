// Auto-generated unit tests for transaction MPTokenIssuanceCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/MPTokenIssuanceCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsMPTokenIssuanceCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenIssuanceCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetScaleValue = canonical_UINT8();
    auto const transferFeeValue = canonical_UINT16();
    auto const maximumAmountValue = canonical_UINT64();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const domainIDValue = canonical_UINT256();
    auto const mutableFlagsValue = canonical_UINT32();

    MPTokenIssuanceCreateBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAssetScale(assetScaleValue);
    builder.setTransferFee(transferFeeValue);
    builder.setMaximumAmount(maximumAmountValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);
    builder.setDomainID(domainIDValue);
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
    // Verify optional fields
    {
        auto const& expected = assetScaleValue;
        auto const actualOpt = tx.getAssetScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetScale should be present";
        expectEqualField(expected, *actualOpt, "sfAssetScale");
        EXPECT_TRUE(tx.hasAssetScale());
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = tx.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(tx.hasTransferFee());
    }

    {
        auto const& expected = maximumAmountValue;
        auto const actualOpt = tx.getMaximumAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaximumAmount should be present";
        expectEqualField(expected, *actualOpt, "sfMaximumAmount");
        EXPECT_TRUE(tx.hasMaximumAmount());
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = tx.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenMetadata should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
        EXPECT_TRUE(tx.hasMPTokenMetadata());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
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
TEST(TransactionsMPTokenIssuanceCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenIssuanceCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetScaleValue = canonical_UINT8();
    auto const transferFeeValue = canonical_UINT16();
    auto const maximumAmountValue = canonical_UINT64();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const domainIDValue = canonical_UINT256();
    auto const mutableFlagsValue = canonical_UINT32();

    // Build an initial transaction
    MPTokenIssuanceCreateBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAssetScale(assetScaleValue);
    initialBuilder.setTransferFee(transferFeeValue);
    initialBuilder.setMaximumAmount(maximumAmountValue);
    initialBuilder.setMPTokenMetadata(mPTokenMetadataValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setMutableFlags(mutableFlagsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    MPTokenIssuanceCreateBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = assetScaleValue;
        auto const actualOpt = rebuiltTx.getAssetScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetScale should be present";
        expectEqualField(expected, *actualOpt, "sfAssetScale");
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = rebuiltTx.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
    }

    {
        auto const& expected = maximumAmountValue;
        auto const actualOpt = rebuiltTx.getMaximumAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaximumAmount should be present";
        expectEqualField(expected, *actualOpt, "sfMaximumAmount");
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = rebuiltTx.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenMetadata should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

    {
        auto const& expected = mutableFlagsValue;
        auto const actualOpt = rebuiltTx.getMutableFlags();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMutableFlags should be present";
        expectEqualField(expected, *actualOpt, "sfMutableFlags");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsMPTokenIssuanceCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(MPTokenIssuanceCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsMPTokenIssuanceCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(MPTokenIssuanceCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsMPTokenIssuanceCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenIssuanceCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    MPTokenIssuanceCreateBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAssetScale());
    EXPECT_FALSE(tx.getAssetScale().has_value());
    EXPECT_FALSE(tx.hasTransferFee());
    EXPECT_FALSE(tx.getTransferFee().has_value());
    EXPECT_FALSE(tx.hasMaximumAmount());
    EXPECT_FALSE(tx.getMaximumAmount().has_value());
    EXPECT_FALSE(tx.hasMPTokenMetadata());
    EXPECT_FALSE(tx.getMPTokenMetadata().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
    EXPECT_FALSE(tx.hasMutableFlags());
    EXPECT_FALSE(tx.getMutableFlags().has_value());
}

}
