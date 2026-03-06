// Auto-generated unit tests for transaction OracleSet

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/OracleSet.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsOracleSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testOracleSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const oracleDocumentIDValue = canonical_UINT32();
    auto const providerValue = canonical_VL();
    auto const uRIValue = canonical_VL();
    auto const assetClassValue = canonical_VL();
    auto const lastUpdateTimeValue = canonical_UINT32();
    auto const priceDataSeriesValue = canonical_ARRAY();

    OracleSetBuilder builder{
        accountValue,
        oracleDocumentIDValue,
        lastUpdateTimeValue,
        priceDataSeriesValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setProvider(providerValue);
    builder.setURI(uRIValue);
    builder.setAssetClass(assetClassValue);

    std::string reason;
    EXPECT_TRUE(builder.validate(reason)) << reason;

    auto tx = builder.build(publicKey, secretKey);

    EXPECT_TRUE(tx->validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx->getSigningPubKey().empty());
    EXPECT_TRUE(tx->hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx->getAccount(), accountValue);
    EXPECT_EQ(tx->getSequence(), sequenceValue);
    EXPECT_EQ(tx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = oracleDocumentIDValue;
        auto const actual = tx->getOracleDocumentID();
        expectEqualField(expected, actual, "sfOracleDocumentID");
    }

    {
        auto const& expected = lastUpdateTimeValue;
        auto const actual = tx->getLastUpdateTime();
        expectEqualField(expected, actual, "sfLastUpdateTime");
    }

    {
        auto const& expected = priceDataSeriesValue;
        auto const actual = tx->getPriceDataSeries();
        expectEqualField(expected, actual, "sfPriceDataSeries");
    }

    // Verify optional fields
    {
        auto const& expected = providerValue;
        auto const actualOpt = tx->getProvider();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfProvider should be present";
        expectEqualField(expected, *actualOpt, "sfProvider");
        EXPECT_TRUE(tx->hasProvider());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = tx->getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(tx->hasURI());
    }

    {
        auto const& expected = assetClassValue;
        auto const actualOpt = tx->getAssetClass();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetClass should be present";
        expectEqualField(expected, *actualOpt, "sfAssetClass");
        EXPECT_TRUE(tx->hasAssetClass());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsOracleSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testOracleSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const oracleDocumentIDValue = canonical_UINT32();
    auto const providerValue = canonical_VL();
    auto const uRIValue = canonical_VL();
    auto const assetClassValue = canonical_VL();
    auto const lastUpdateTimeValue = canonical_UINT32();
    auto const priceDataSeriesValue = canonical_ARRAY();

    // Build an initial transaction
    OracleSetBuilder initialBuilder{
        accountValue,
        oracleDocumentIDValue,
        lastUpdateTimeValue,
        priceDataSeriesValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setProvider(providerValue);
    initialBuilder.setURI(uRIValue);
    initialBuilder.setAssetClass(assetClassValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    OracleSetBuilder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = oracleDocumentIDValue;
        auto const actual = rebuiltTx->getOracleDocumentID();
        expectEqualField(expected, actual, "sfOracleDocumentID");
    }

    {
        auto const& expected = lastUpdateTimeValue;
        auto const actual = rebuiltTx->getLastUpdateTime();
        expectEqualField(expected, actual, "sfLastUpdateTime");
    }

    {
        auto const& expected = priceDataSeriesValue;
        auto const actual = rebuiltTx->getPriceDataSeries();
        expectEqualField(expected, actual, "sfPriceDataSeries");
    }

    // Verify optional fields
    {
        auto const& expected = providerValue;
        auto const actualOpt = rebuiltTx->getProvider();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfProvider should be present";
        expectEqualField(expected, *actualOpt, "sfProvider");
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = rebuiltTx->getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
    }

    {
        auto const& expected = assetClassValue;
        auto const actualOpt = rebuiltTx->getAssetClass();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetClass should be present";
        expectEqualField(expected, *actualOpt, "sfAssetClass");
    }

}

}
