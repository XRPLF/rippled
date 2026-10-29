// Auto-generated unit tests for transaction CouponScheduleCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/CouponScheduleCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsCouponScheduleCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testCouponScheduleCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const couponAssetValue = canonical_ISSUE();
    auto const couponAmountValue = canonical_AMOUNT();
    auto const couponIntervalValue = canonical_UINT32();
    auto const firstCouponTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const callNoticePeriodValue = canonical_UINT32();
    auto const earliestCallTimeValue = canonical_UINT32();

    CouponScheduleCreateBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        couponAssetValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setCouponAmount(couponAmountValue);
    builder.setCouponInterval(couponIntervalValue);
    builder.setFirstCouponTime(firstCouponTimeValue);
    builder.setExpiration(expirationValue);
    builder.setCallNoticePeriod(callNoticePeriodValue);
    builder.setEarliestCallTime(earliestCallTimeValue);

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
        auto const& expected = couponAssetValue;
        auto const actual = tx.getCouponAsset();
        expectEqualField(expected, actual, "sfCouponAsset");
    }

    // Verify optional fields
    {
        auto const& expected = couponAmountValue;
        auto const actualOpt = tx.getCouponAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCouponAmount should be present";
        expectEqualField(expected, *actualOpt, "sfCouponAmount");
        EXPECT_TRUE(tx.hasCouponAmount());
    }

    {
        auto const& expected = couponIntervalValue;
        auto const actualOpt = tx.getCouponInterval();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCouponInterval should be present";
        expectEqualField(expected, *actualOpt, "sfCouponInterval");
        EXPECT_TRUE(tx.hasCouponInterval());
    }

    {
        auto const& expected = firstCouponTimeValue;
        auto const actualOpt = tx.getFirstCouponTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFirstCouponTime should be present";
        expectEqualField(expected, *actualOpt, "sfFirstCouponTime");
        EXPECT_TRUE(tx.hasFirstCouponTime());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx.hasExpiration());
    }

    {
        auto const& expected = callNoticePeriodValue;
        auto const actualOpt = tx.getCallNoticePeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCallNoticePeriod should be present";
        expectEqualField(expected, *actualOpt, "sfCallNoticePeriod");
        EXPECT_TRUE(tx.hasCallNoticePeriod());
    }

    {
        auto const& expected = earliestCallTimeValue;
        auto const actualOpt = tx.getEarliestCallTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfEarliestCallTime should be present";
        expectEqualField(expected, *actualOpt, "sfEarliestCallTime");
        EXPECT_TRUE(tx.hasEarliestCallTime());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsCouponScheduleCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testCouponScheduleCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const couponAssetValue = canonical_ISSUE();
    auto const couponAmountValue = canonical_AMOUNT();
    auto const couponIntervalValue = canonical_UINT32();
    auto const firstCouponTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const callNoticePeriodValue = canonical_UINT32();
    auto const earliestCallTimeValue = canonical_UINT32();

    // Build an initial transaction
    CouponScheduleCreateBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        couponAssetValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setCouponAmount(couponAmountValue);
    initialBuilder.setCouponInterval(couponIntervalValue);
    initialBuilder.setFirstCouponTime(firstCouponTimeValue);
    initialBuilder.setExpiration(expirationValue);
    initialBuilder.setCallNoticePeriod(callNoticePeriodValue);
    initialBuilder.setEarliestCallTime(earliestCallTimeValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    CouponScheduleCreateBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = couponAssetValue;
        auto const actual = rebuiltTx.getCouponAsset();
        expectEqualField(expected, actual, "sfCouponAsset");
    }

    // Verify optional fields
    {
        auto const& expected = couponAmountValue;
        auto const actualOpt = rebuiltTx.getCouponAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCouponAmount should be present";
        expectEqualField(expected, *actualOpt, "sfCouponAmount");
    }

    {
        auto const& expected = couponIntervalValue;
        auto const actualOpt = rebuiltTx.getCouponInterval();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCouponInterval should be present";
        expectEqualField(expected, *actualOpt, "sfCouponInterval");
    }

    {
        auto const& expected = firstCouponTimeValue;
        auto const actualOpt = rebuiltTx.getFirstCouponTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFirstCouponTime should be present";
        expectEqualField(expected, *actualOpt, "sfFirstCouponTime");
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

    {
        auto const& expected = callNoticePeriodValue;
        auto const actualOpt = rebuiltTx.getCallNoticePeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCallNoticePeriod should be present";
        expectEqualField(expected, *actualOpt, "sfCallNoticePeriod");
    }

    {
        auto const& expected = earliestCallTimeValue;
        auto const actualOpt = rebuiltTx.getEarliestCallTime();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfEarliestCallTime should be present";
        expectEqualField(expected, *actualOpt, "sfEarliestCallTime");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsCouponScheduleCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CouponScheduleCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsCouponScheduleCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CouponScheduleCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsCouponScheduleCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testCouponScheduleCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const couponAssetValue = canonical_ISSUE();

    CouponScheduleCreateBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        couponAssetValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCouponAmount());
    EXPECT_FALSE(tx.getCouponAmount().has_value());
    EXPECT_FALSE(tx.hasCouponInterval());
    EXPECT_FALSE(tx.getCouponInterval().has_value());
    EXPECT_FALSE(tx.hasFirstCouponTime());
    EXPECT_FALSE(tx.getFirstCouponTime().has_value());
    EXPECT_FALSE(tx.hasExpiration());
    EXPECT_FALSE(tx.getExpiration().has_value());
    EXPECT_FALSE(tx.hasCallNoticePeriod());
    EXPECT_FALSE(tx.getCallNoticePeriod().has_value());
    EXPECT_FALSE(tx.hasEarliestCallTime());
    EXPECT_FALSE(tx.getEarliestCallTime().has_value());
}

}
