// Auto-generated unit tests for transaction PerpExchangeSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/PerpExchangeSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPerpExchangeSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpExchangeSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const exchangeIDValue = canonical_UINT256();
    auto const assetValue = canonical_ISSUE();
    auto const scaleValue = canonical_UINT8();
    auto const domainIDValue = canonical_UINT256();
    auto const makerFeeValue = canonical_UINT32();
    auto const takerFeeValue = canonical_UINT32();
    auto const ownerFeeShareValue = canonical_UINT32();
    auto const builderFeeMaxValue = canonical_UINT32();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const liquidationPenaltyValue = canonical_UINT32();
    auto const liquidationRewardMinValue = canonical_NUMBER();
    auto const liquidationChunkMinValue = canonical_NUMBER();
    auto const dataValue = canonical_VL();

    PerpExchangeSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setExchangeID(exchangeIDValue);
    builder.setAsset(assetValue);
    builder.setScale(scaleValue);
    builder.setDomainID(domainIDValue);
    builder.setMakerFee(makerFeeValue);
    builder.setTakerFee(takerFeeValue);
    builder.setOwnerFeeShare(ownerFeeShareValue);
    builder.setBuilderFeeMax(builderFeeMaxValue);
    builder.setCoverRateMinimum(coverRateMinimumValue);
    builder.setLiquidationPenalty(liquidationPenaltyValue);
    builder.setLiquidationRewardMin(liquidationRewardMinValue);
    builder.setLiquidationChunkMin(liquidationChunkMinValue);
    builder.setData(dataValue);

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
        auto const& expected = exchangeIDValue;
        auto const actualOpt = tx.getExchangeID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExchangeID should be present";
        expectEqualField(expected, *actualOpt, "sfExchangeID");
        EXPECT_TRUE(tx.hasExchangeID());
    }

    {
        auto const& expected = assetValue;
        auto const actualOpt = tx.getAsset();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAsset should be present";
        expectEqualField(expected, *actualOpt, "sfAsset");
        EXPECT_TRUE(tx.hasAsset());
    }

    {
        auto const& expected = scaleValue;
        auto const actualOpt = tx.getScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfScale should be present";
        expectEqualField(expected, *actualOpt, "sfScale");
        EXPECT_TRUE(tx.hasScale());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
    }

    {
        auto const& expected = makerFeeValue;
        auto const actualOpt = tx.getMakerFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMakerFee should be present";
        expectEqualField(expected, *actualOpt, "sfMakerFee");
        EXPECT_TRUE(tx.hasMakerFee());
    }

    {
        auto const& expected = takerFeeValue;
        auto const actualOpt = tx.getTakerFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTakerFee should be present";
        expectEqualField(expected, *actualOpt, "sfTakerFee");
        EXPECT_TRUE(tx.hasTakerFee());
    }

    {
        auto const& expected = ownerFeeShareValue;
        auto const actualOpt = tx.getOwnerFeeShare();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwnerFeeShare should be present";
        expectEqualField(expected, *actualOpt, "sfOwnerFeeShare");
        EXPECT_TRUE(tx.hasOwnerFeeShare());
    }

    {
        auto const& expected = builderFeeMaxValue;
        auto const actualOpt = tx.getBuilderFeeMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBuilderFeeMax should be present";
        expectEqualField(expected, *actualOpt, "sfBuilderFeeMax");
        EXPECT_TRUE(tx.hasBuilderFeeMax());
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = tx.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCoverRateMinimum should be present";
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
        EXPECT_TRUE(tx.hasCoverRateMinimum());
    }

    {
        auto const& expected = liquidationPenaltyValue;
        auto const actualOpt = tx.getLiquidationPenalty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLiquidationPenalty should be present";
        expectEqualField(expected, *actualOpt, "sfLiquidationPenalty");
        EXPECT_TRUE(tx.hasLiquidationPenalty());
    }

    {
        auto const& expected = liquidationRewardMinValue;
        auto const actualOpt = tx.getLiquidationRewardMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLiquidationRewardMin should be present";
        expectEqualField(expected, *actualOpt, "sfLiquidationRewardMin");
        EXPECT_TRUE(tx.hasLiquidationRewardMin());
    }

    {
        auto const& expected = liquidationChunkMinValue;
        auto const actualOpt = tx.getLiquidationChunkMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLiquidationChunkMin should be present";
        expectEqualField(expected, *actualOpt, "sfLiquidationChunkMin");
        EXPECT_TRUE(tx.hasLiquidationChunkMin());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = tx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx.hasData());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPerpExchangeSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpExchangeSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const exchangeIDValue = canonical_UINT256();
    auto const assetValue = canonical_ISSUE();
    auto const scaleValue = canonical_UINT8();
    auto const domainIDValue = canonical_UINT256();
    auto const makerFeeValue = canonical_UINT32();
    auto const takerFeeValue = canonical_UINT32();
    auto const ownerFeeShareValue = canonical_UINT32();
    auto const builderFeeMaxValue = canonical_UINT32();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const liquidationPenaltyValue = canonical_UINT32();
    auto const liquidationRewardMinValue = canonical_NUMBER();
    auto const liquidationChunkMinValue = canonical_NUMBER();
    auto const dataValue = canonical_VL();

    // Build an initial transaction
    PerpExchangeSetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setExchangeID(exchangeIDValue);
    initialBuilder.setAsset(assetValue);
    initialBuilder.setScale(scaleValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setMakerFee(makerFeeValue);
    initialBuilder.setTakerFee(takerFeeValue);
    initialBuilder.setOwnerFeeShare(ownerFeeShareValue);
    initialBuilder.setBuilderFeeMax(builderFeeMaxValue);
    initialBuilder.setCoverRateMinimum(coverRateMinimumValue);
    initialBuilder.setLiquidationPenalty(liquidationPenaltyValue);
    initialBuilder.setLiquidationRewardMin(liquidationRewardMinValue);
    initialBuilder.setLiquidationChunkMin(liquidationChunkMinValue);
    initialBuilder.setData(dataValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PerpExchangeSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = exchangeIDValue;
        auto const actualOpt = rebuiltTx.getExchangeID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExchangeID should be present";
        expectEqualField(expected, *actualOpt, "sfExchangeID");
    }

    {
        auto const& expected = assetValue;
        auto const actualOpt = rebuiltTx.getAsset();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAsset should be present";
        expectEqualField(expected, *actualOpt, "sfAsset");
    }

    {
        auto const& expected = scaleValue;
        auto const actualOpt = rebuiltTx.getScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfScale should be present";
        expectEqualField(expected, *actualOpt, "sfScale");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

    {
        auto const& expected = makerFeeValue;
        auto const actualOpt = rebuiltTx.getMakerFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMakerFee should be present";
        expectEqualField(expected, *actualOpt, "sfMakerFee");
    }

    {
        auto const& expected = takerFeeValue;
        auto const actualOpt = rebuiltTx.getTakerFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTakerFee should be present";
        expectEqualField(expected, *actualOpt, "sfTakerFee");
    }

    {
        auto const& expected = ownerFeeShareValue;
        auto const actualOpt = rebuiltTx.getOwnerFeeShare();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwnerFeeShare should be present";
        expectEqualField(expected, *actualOpt, "sfOwnerFeeShare");
    }

    {
        auto const& expected = builderFeeMaxValue;
        auto const actualOpt = rebuiltTx.getBuilderFeeMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBuilderFeeMax should be present";
        expectEqualField(expected, *actualOpt, "sfBuilderFeeMax");
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = rebuiltTx.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCoverRateMinimum should be present";
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
    }

    {
        auto const& expected = liquidationPenaltyValue;
        auto const actualOpt = rebuiltTx.getLiquidationPenalty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLiquidationPenalty should be present";
        expectEqualField(expected, *actualOpt, "sfLiquidationPenalty");
    }

    {
        auto const& expected = liquidationRewardMinValue;
        auto const actualOpt = rebuiltTx.getLiquidationRewardMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLiquidationRewardMin should be present";
        expectEqualField(expected, *actualOpt, "sfLiquidationRewardMin");
    }

    {
        auto const& expected = liquidationChunkMinValue;
        auto const actualOpt = rebuiltTx.getLiquidationChunkMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLiquidationChunkMin should be present";
        expectEqualField(expected, *actualOpt, "sfLiquidationChunkMin");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPerpExchangeSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpExchangeSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPerpExchangeSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpExchangeSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPerpExchangeSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpExchangeSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    PerpExchangeSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasExchangeID());
    EXPECT_FALSE(tx.getExchangeID().has_value());
    EXPECT_FALSE(tx.hasAsset());
    EXPECT_FALSE(tx.getAsset().has_value());
    EXPECT_FALSE(tx.hasScale());
    EXPECT_FALSE(tx.getScale().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
    EXPECT_FALSE(tx.hasMakerFee());
    EXPECT_FALSE(tx.getMakerFee().has_value());
    EXPECT_FALSE(tx.hasTakerFee());
    EXPECT_FALSE(tx.getTakerFee().has_value());
    EXPECT_FALSE(tx.hasOwnerFeeShare());
    EXPECT_FALSE(tx.getOwnerFeeShare().has_value());
    EXPECT_FALSE(tx.hasBuilderFeeMax());
    EXPECT_FALSE(tx.getBuilderFeeMax().has_value());
    EXPECT_FALSE(tx.hasCoverRateMinimum());
    EXPECT_FALSE(tx.getCoverRateMinimum().has_value());
    EXPECT_FALSE(tx.hasLiquidationPenalty());
    EXPECT_FALSE(tx.getLiquidationPenalty().has_value());
    EXPECT_FALSE(tx.hasLiquidationRewardMin());
    EXPECT_FALSE(tx.getLiquidationRewardMin().has_value());
    EXPECT_FALSE(tx.hasLiquidationChunkMin());
    EXPECT_FALSE(tx.getLiquidationChunkMin().has_value());
    EXPECT_FALSE(tx.hasData());
    EXPECT_FALSE(tx.getData().has_value());
}

}
