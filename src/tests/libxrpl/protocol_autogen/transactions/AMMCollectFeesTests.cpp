// Auto-generated unit tests for transaction AMMCollectFees


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMCollectFees.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAMMCollectFeesTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMCollectFees"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const curveTypeValue = canonical_UINT8();
    auto const positionIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();

    AMMCollectFeesBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCurveType(curveTypeValue);
    builder.setPositionID(positionIDValue);
    builder.setBinID(binIDValue);

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
        auto const& expected = assetValue;
        auto const actual = tx.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = tx.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = tx.getCurveType();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCurveType should be present";
        expectEqualField(expected, *actualOpt, "sfCurveType");
        EXPECT_TRUE(tx.hasCurveType());
    }

    {
        auto const& expected = positionIDValue;
        auto const actualOpt = tx.getPositionID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPositionID should be present";
        expectEqualField(expected, *actualOpt, "sfPositionID");
        EXPECT_TRUE(tx.hasPositionID());
    }

    {
        auto const& expected = binIDValue;
        auto const actualOpt = tx.getBinID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBinID should be present";
        expectEqualField(expected, *actualOpt, "sfBinID");
        EXPECT_TRUE(tx.hasBinID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAMMCollectFeesTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMCollectFeesFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const curveTypeValue = canonical_UINT8();
    auto const positionIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();

    // Build an initial transaction
    AMMCollectFeesBuilder initialBuilder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCurveType(curveTypeValue);
    initialBuilder.setPositionID(positionIDValue);
    initialBuilder.setBinID(binIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AMMCollectFeesBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = assetValue;
        auto const actual = rebuiltTx.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = rebuiltTx.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = rebuiltTx.getCurveType();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCurveType should be present";
        expectEqualField(expected, *actualOpt, "sfCurveType");
    }

    {
        auto const& expected = positionIDValue;
        auto const actualOpt = rebuiltTx.getPositionID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPositionID should be present";
        expectEqualField(expected, *actualOpt, "sfPositionID");
    }

    {
        auto const& expected = binIDValue;
        auto const actualOpt = rebuiltTx.getBinID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBinID should be present";
        expectEqualField(expected, *actualOpt, "sfBinID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAMMCollectFeesTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMCollectFees{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAMMCollectFeesTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMCollectFeesBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAMMCollectFeesTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testAMMCollectFeesNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();

    AMMCollectFeesBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCurveType());
    EXPECT_FALSE(tx.getCurveType().has_value());
    EXPECT_FALSE(tx.hasPositionID());
    EXPECT_FALSE(tx.getPositionID().has_value());
    EXPECT_FALSE(tx.hasBinID());
    EXPECT_FALSE(tx.getBinID().has_value());
}

}
