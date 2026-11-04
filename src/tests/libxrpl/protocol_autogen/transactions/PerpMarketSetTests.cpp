// Auto-generated unit tests for transaction PerpMarketSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/PerpMarketSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPerpMarketSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpMarketSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const exchangeIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const baseAssetValue = canonical_CURRENCY();
    auto const quoteAssetValue = canonical_CURRENCY();
    auto const indexOraclesValue = canonical_ARRAY();
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();

    PerpMarketSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setExchangeID(exchangeIDValue);
    builder.setMarketID(marketIDValue);
    builder.setBaseAsset(baseAssetValue);
    builder.setQuoteAsset(quoteAssetValue);
    builder.setIndexOracles(indexOraclesValue);
    builder.setIndexMaxLedgers(indexMaxLedgersValue);
    builder.setPriceTick(priceTickValue);
    builder.setLotSize(lotSizeValue);
    builder.setMarginTiers(marginTiersValue);
    builder.setOpenInterestMax(openInterestMaxValue);
    builder.setOrderPriceBand(orderPriceBandValue);

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
        auto const& expected = marketIDValue;
        auto const actualOpt = tx.getMarketID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarketID should be present";
        expectEqualField(expected, *actualOpt, "sfMarketID");
        EXPECT_TRUE(tx.hasMarketID());
    }

    {
        auto const& expected = baseAssetValue;
        auto const actualOpt = tx.getBaseAsset();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBaseAsset should be present";
        expectEqualField(expected, *actualOpt, "sfBaseAsset");
        EXPECT_TRUE(tx.hasBaseAsset());
    }

    {
        auto const& expected = quoteAssetValue;
        auto const actualOpt = tx.getQuoteAsset();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfQuoteAsset should be present";
        expectEqualField(expected, *actualOpt, "sfQuoteAsset");
        EXPECT_TRUE(tx.hasQuoteAsset());
    }

    {
        auto const& expected = indexOraclesValue;
        auto const actualOpt = tx.getIndexOracles();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIndexOracles should be present";
        expectEqualField(expected, *actualOpt, "sfIndexOracles");
        EXPECT_TRUE(tx.hasIndexOracles());
    }

    {
        auto const& expected = indexMaxLedgersValue;
        auto const actualOpt = tx.getIndexMaxLedgers();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIndexMaxLedgers should be present";
        expectEqualField(expected, *actualOpt, "sfIndexMaxLedgers");
        EXPECT_TRUE(tx.hasIndexMaxLedgers());
    }

    {
        auto const& expected = priceTickValue;
        auto const actualOpt = tx.getPriceTick();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPriceTick should be present";
        expectEqualField(expected, *actualOpt, "sfPriceTick");
        EXPECT_TRUE(tx.hasPriceTick());
    }

    {
        auto const& expected = lotSizeValue;
        auto const actualOpt = tx.getLotSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLotSize should be present";
        expectEqualField(expected, *actualOpt, "sfLotSize");
        EXPECT_TRUE(tx.hasLotSize());
    }

    {
        auto const& expected = marginTiersValue;
        auto const actualOpt = tx.getMarginTiers();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarginTiers should be present";
        expectEqualField(expected, *actualOpt, "sfMarginTiers");
        EXPECT_TRUE(tx.hasMarginTiers());
    }

    {
        auto const& expected = openInterestMaxValue;
        auto const actualOpt = tx.getOpenInterestMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOpenInterestMax should be present";
        expectEqualField(expected, *actualOpt, "sfOpenInterestMax");
        EXPECT_TRUE(tx.hasOpenInterestMax());
    }

    {
        auto const& expected = orderPriceBandValue;
        auto const actualOpt = tx.getOrderPriceBand();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOrderPriceBand should be present";
        expectEqualField(expected, *actualOpt, "sfOrderPriceBand");
        EXPECT_TRUE(tx.hasOrderPriceBand());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPerpMarketSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpMarketSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const exchangeIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const baseAssetValue = canonical_CURRENCY();
    auto const quoteAssetValue = canonical_CURRENCY();
    auto const indexOraclesValue = canonical_ARRAY();
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();

    // Build an initial transaction
    PerpMarketSetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setExchangeID(exchangeIDValue);
    initialBuilder.setMarketID(marketIDValue);
    initialBuilder.setBaseAsset(baseAssetValue);
    initialBuilder.setQuoteAsset(quoteAssetValue);
    initialBuilder.setIndexOracles(indexOraclesValue);
    initialBuilder.setIndexMaxLedgers(indexMaxLedgersValue);
    initialBuilder.setPriceTick(priceTickValue);
    initialBuilder.setLotSize(lotSizeValue);
    initialBuilder.setMarginTiers(marginTiersValue);
    initialBuilder.setOpenInterestMax(openInterestMaxValue);
    initialBuilder.setOrderPriceBand(orderPriceBandValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PerpMarketSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = marketIDValue;
        auto const actualOpt = rebuiltTx.getMarketID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarketID should be present";
        expectEqualField(expected, *actualOpt, "sfMarketID");
    }

    {
        auto const& expected = baseAssetValue;
        auto const actualOpt = rebuiltTx.getBaseAsset();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBaseAsset should be present";
        expectEqualField(expected, *actualOpt, "sfBaseAsset");
    }

    {
        auto const& expected = quoteAssetValue;
        auto const actualOpt = rebuiltTx.getQuoteAsset();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfQuoteAsset should be present";
        expectEqualField(expected, *actualOpt, "sfQuoteAsset");
    }

    {
        auto const& expected = indexOraclesValue;
        auto const actualOpt = rebuiltTx.getIndexOracles();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIndexOracles should be present";
        expectEqualField(expected, *actualOpt, "sfIndexOracles");
    }

    {
        auto const& expected = indexMaxLedgersValue;
        auto const actualOpt = rebuiltTx.getIndexMaxLedgers();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIndexMaxLedgers should be present";
        expectEqualField(expected, *actualOpt, "sfIndexMaxLedgers");
    }

    {
        auto const& expected = priceTickValue;
        auto const actualOpt = rebuiltTx.getPriceTick();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPriceTick should be present";
        expectEqualField(expected, *actualOpt, "sfPriceTick");
    }

    {
        auto const& expected = lotSizeValue;
        auto const actualOpt = rebuiltTx.getLotSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLotSize should be present";
        expectEqualField(expected, *actualOpt, "sfLotSize");
    }

    {
        auto const& expected = marginTiersValue;
        auto const actualOpt = rebuiltTx.getMarginTiers();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarginTiers should be present";
        expectEqualField(expected, *actualOpt, "sfMarginTiers");
    }

    {
        auto const& expected = openInterestMaxValue;
        auto const actualOpt = rebuiltTx.getOpenInterestMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOpenInterestMax should be present";
        expectEqualField(expected, *actualOpt, "sfOpenInterestMax");
    }

    {
        auto const& expected = orderPriceBandValue;
        auto const actualOpt = rebuiltTx.getOrderPriceBand();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOrderPriceBand should be present";
        expectEqualField(expected, *actualOpt, "sfOrderPriceBand");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPerpMarketSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpMarketSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPerpMarketSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpMarketSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPerpMarketSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpMarketSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    PerpMarketSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasExchangeID());
    EXPECT_FALSE(tx.getExchangeID().has_value());
    EXPECT_FALSE(tx.hasMarketID());
    EXPECT_FALSE(tx.getMarketID().has_value());
    EXPECT_FALSE(tx.hasBaseAsset());
    EXPECT_FALSE(tx.getBaseAsset().has_value());
    EXPECT_FALSE(tx.hasQuoteAsset());
    EXPECT_FALSE(tx.getQuoteAsset().has_value());
    EXPECT_FALSE(tx.hasIndexOracles());
    EXPECT_FALSE(tx.getIndexOracles().has_value());
    EXPECT_FALSE(tx.hasIndexMaxLedgers());
    EXPECT_FALSE(tx.getIndexMaxLedgers().has_value());
    EXPECT_FALSE(tx.hasPriceTick());
    EXPECT_FALSE(tx.getPriceTick().has_value());
    EXPECT_FALSE(tx.hasLotSize());
    EXPECT_FALSE(tx.getLotSize().has_value());
    EXPECT_FALSE(tx.hasMarginTiers());
    EXPECT_FALSE(tx.getMarginTiers().has_value());
    EXPECT_FALSE(tx.hasOpenInterestMax());
    EXPECT_FALSE(tx.getOpenInterestMax().has_value());
    EXPECT_FALSE(tx.hasOrderPriceBand());
    EXPECT_FALSE(tx.getOrderPriceBand().has_value());
}

}
