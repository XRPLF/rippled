// Auto-generated unit tests for transaction OracleSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/OracleSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

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
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setOracleDocumentID(oracleDocumentIDValue);
    builder.setProvider(providerValue);
    builder.setURI(uRIValue);
    builder.setAssetClass(assetClassValue);
    builder.setLastUpdateTime(lastUpdateTimeValue);
    builder.setPriceDataSeries(priceDataSeriesValue);

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
        auto const& expected = oracleDocumentIDValue;
        auto const actualOpt = tx.getOracleDocumentID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOracleDocumentID should be present";
        expectEqualField(expected, *actualOpt, "sfOracleDocumentID");
        EXPECT_TRUE(tx.hasOracleDocumentID());
    }

    {
        auto const& expected = providerValue;
        auto const actualOpt = tx.getProvider();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfProvider should be present";
        expectEqualField(expected, *actualOpt, "sfProvider");
        EXPECT_TRUE(tx.hasProvider());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = tx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(tx.hasURI());
    }

    {
        auto const& expected = assetClassValue;
        auto const actualOpt = tx.getAssetClass();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetClass should be present";
        expectEqualField(expected, *actualOpt, "sfAssetClass");
        EXPECT_TRUE(tx.hasAssetClass());
    }

    {
        auto const& expected = lastUpdateTimeValue;
        auto const actualOpt = tx.getLastUpdateTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLastUpdateTime should be present";
        expectEqualField(expected, *actualOpt, "sfLastUpdateTime");
        EXPECT_TRUE(tx.hasLastUpdateTime());
    }

    {
        auto const& expected = priceDataSeriesValue;
        auto const actualOpt = tx.getPriceDataSeries();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPriceDataSeries should be present";
        expectEqualField(expected, *actualOpt, "sfPriceDataSeries");
        EXPECT_TRUE(tx.hasPriceDataSeries());
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
        sequenceValue,
        feeValue
    };

    initialBuilder.setOracleDocumentID(oracleDocumentIDValue);
    initialBuilder.setProvider(providerValue);
    initialBuilder.setURI(uRIValue);
    initialBuilder.setAssetClass(assetClassValue);
    initialBuilder.setLastUpdateTime(lastUpdateTimeValue);
    initialBuilder.setPriceDataSeries(priceDataSeriesValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    OracleSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = oracleDocumentIDValue;
        auto const actualOpt = rebuiltTx.getOracleDocumentID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOracleDocumentID should be present";
        expectEqualField(expected, *actualOpt, "sfOracleDocumentID");
    }

    {
        auto const& expected = providerValue;
        auto const actualOpt = rebuiltTx.getProvider();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfProvider should be present";
        expectEqualField(expected, *actualOpt, "sfProvider");
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = rebuiltTx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
    }

    {
        auto const& expected = assetClassValue;
        auto const actualOpt = rebuiltTx.getAssetClass();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetClass should be present";
        expectEqualField(expected, *actualOpt, "sfAssetClass");
    }

    {
        auto const& expected = lastUpdateTimeValue;
        auto const actualOpt = rebuiltTx.getLastUpdateTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLastUpdateTime should be present";
        expectEqualField(expected, *actualOpt, "sfLastUpdateTime");
    }

    {
        auto const& expected = priceDataSeriesValue;
        auto const actualOpt = rebuiltTx.getPriceDataSeries();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPriceDataSeries should be present";
        expectEqualField(expected, *actualOpt, "sfPriceDataSeries");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsOracleSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(OracleSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsOracleSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(OracleSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsOracleSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testOracleSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    OracleSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasOracleDocumentID());
    EXPECT_FALSE(tx.getOracleDocumentID().has_value());
    EXPECT_FALSE(tx.hasProvider());
    EXPECT_FALSE(tx.getProvider().has_value());
    EXPECT_FALSE(tx.hasURI());
    EXPECT_FALSE(tx.getURI().has_value());
    EXPECT_FALSE(tx.hasAssetClass());
    EXPECT_FALSE(tx.getAssetClass().has_value());
    EXPECT_FALSE(tx.hasLastUpdateTime());
    EXPECT_FALSE(tx.getLastUpdateTime().has_value());
    EXPECT_FALSE(tx.hasPriceDataSeries());
    EXPECT_FALSE(tx.getPriceDataSeries().has_value());
}

}
