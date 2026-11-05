// Auto-generated unit tests for transaction VaultCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/VaultCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsVaultCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const assetsMaximumValue = canonical_NUMBER();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const domainIDValue = canonical_UINT256();
    auto const withdrawalPolicyValue = canonical_UINT8();
    auto const dataValue = canonical_VL();
    auto const scaleValue = canonical_UINT8();
    auto const vaultKindValue = canonical_UINT8();
    auto const subscriptionDateValue = canonical_UINT32();
    auto const redemptionDateValue = canonical_UINT32();
    auto const dealingIntervalValue = canonical_UINT32();
    auto const dealingWindowValue = canonical_UINT32();
    auto const depositFeeValue = canonical_UINT32();
    auto const redemptionFeeValue = canonical_UINT32();
    auto const redemptionPeriodValue = canonical_UINT32();
    auto const accountingMethodValue = canonical_UINT8();

    VaultCreateBuilder builder{
        accountValue,
        assetValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAssetsMaximum(assetsMaximumValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);
    builder.setDomainID(domainIDValue);
    builder.setWithdrawalPolicy(withdrawalPolicyValue);
    builder.setData(dataValue);
    builder.setScale(scaleValue);
    builder.setVaultKind(vaultKindValue);
    builder.setSubscriptionDate(subscriptionDateValue);
    builder.setRedemptionDate(redemptionDateValue);
    builder.setDealingInterval(dealingIntervalValue);
    builder.setDealingWindow(dealingWindowValue);
    builder.setDepositFee(depositFeeValue);
    builder.setRedemptionFee(redemptionFeeValue);
    builder.setRedemptionPeriod(redemptionPeriodValue);
    builder.setAccountingMethod(accountingMethodValue);

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

    // Verify optional fields
    {
        auto const& expected = assetsMaximumValue;
        auto const actualOpt = tx.getAssetsMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetsMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfAssetsMaximum");
        EXPECT_TRUE(tx.hasAssetsMaximum());
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
        auto const& expected = withdrawalPolicyValue;
        auto const actualOpt = tx.getWithdrawalPolicy();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWithdrawalPolicy should be present";
        expectEqualField(expected, *actualOpt, "sfWithdrawalPolicy");
        EXPECT_TRUE(tx.hasWithdrawalPolicy());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = tx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx.hasData());
    }

    {
        auto const& expected = scaleValue;
        auto const actualOpt = tx.getScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfScale should be present";
        expectEqualField(expected, *actualOpt, "sfScale");
        EXPECT_TRUE(tx.hasScale());
    }

    {
        auto const& expected = vaultKindValue;
        auto const actualOpt = tx.getVaultKind();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfVaultKind should be present";
        expectEqualField(expected, *actualOpt, "sfVaultKind");
        EXPECT_TRUE(tx.hasVaultKind());
    }

    {
        auto const& expected = subscriptionDateValue;
        auto const actualOpt = tx.getSubscriptionDate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubscriptionDate should be present";
        expectEqualField(expected, *actualOpt, "sfSubscriptionDate");
        EXPECT_TRUE(tx.hasSubscriptionDate());
    }

    {
        auto const& expected = redemptionDateValue;
        auto const actualOpt = tx.getRedemptionDate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRedemptionDate should be present";
        expectEqualField(expected, *actualOpt, "sfRedemptionDate");
        EXPECT_TRUE(tx.hasRedemptionDate());
    }

    {
        auto const& expected = dealingIntervalValue;
        auto const actualOpt = tx.getDealingInterval();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDealingInterval should be present";
        expectEqualField(expected, *actualOpt, "sfDealingInterval");
        EXPECT_TRUE(tx.hasDealingInterval());
    }

    {
        auto const& expected = dealingWindowValue;
        auto const actualOpt = tx.getDealingWindow();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDealingWindow should be present";
        expectEqualField(expected, *actualOpt, "sfDealingWindow");
        EXPECT_TRUE(tx.hasDealingWindow());
    }

    {
        auto const& expected = depositFeeValue;
        auto const actualOpt = tx.getDepositFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDepositFee should be present";
        expectEqualField(expected, *actualOpt, "sfDepositFee");
        EXPECT_TRUE(tx.hasDepositFee());
    }

    {
        auto const& expected = redemptionFeeValue;
        auto const actualOpt = tx.getRedemptionFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRedemptionFee should be present";
        expectEqualField(expected, *actualOpt, "sfRedemptionFee");
        EXPECT_TRUE(tx.hasRedemptionFee());
    }

    {
        auto const& expected = redemptionPeriodValue;
        auto const actualOpt = tx.getRedemptionPeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRedemptionPeriod should be present";
        expectEqualField(expected, *actualOpt, "sfRedemptionPeriod");
        EXPECT_TRUE(tx.hasRedemptionPeriod());
    }

    {
        auto const& expected = accountingMethodValue;
        auto const actualOpt = tx.getAccountingMethod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAccountingMethod should be present";
        expectEqualField(expected, *actualOpt, "sfAccountingMethod");
        EXPECT_TRUE(tx.hasAccountingMethod());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsVaultCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const assetsMaximumValue = canonical_NUMBER();
    auto const mPTokenMetadataValue = canonical_VL();
    auto const domainIDValue = canonical_UINT256();
    auto const withdrawalPolicyValue = canonical_UINT8();
    auto const dataValue = canonical_VL();
    auto const scaleValue = canonical_UINT8();
    auto const vaultKindValue = canonical_UINT8();
    auto const subscriptionDateValue = canonical_UINT32();
    auto const redemptionDateValue = canonical_UINT32();
    auto const dealingIntervalValue = canonical_UINT32();
    auto const dealingWindowValue = canonical_UINT32();
    auto const depositFeeValue = canonical_UINT32();
    auto const redemptionFeeValue = canonical_UINT32();
    auto const redemptionPeriodValue = canonical_UINT32();
    auto const accountingMethodValue = canonical_UINT8();

    // Build an initial transaction
    VaultCreateBuilder initialBuilder{
        accountValue,
        assetValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAssetsMaximum(assetsMaximumValue);
    initialBuilder.setMPTokenMetadata(mPTokenMetadataValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setWithdrawalPolicy(withdrawalPolicyValue);
    initialBuilder.setData(dataValue);
    initialBuilder.setScale(scaleValue);
    initialBuilder.setVaultKind(vaultKindValue);
    initialBuilder.setSubscriptionDate(subscriptionDateValue);
    initialBuilder.setRedemptionDate(redemptionDateValue);
    initialBuilder.setDealingInterval(dealingIntervalValue);
    initialBuilder.setDealingWindow(dealingWindowValue);
    initialBuilder.setDepositFee(depositFeeValue);
    initialBuilder.setRedemptionFee(redemptionFeeValue);
    initialBuilder.setRedemptionPeriod(redemptionPeriodValue);
    initialBuilder.setAccountingMethod(accountingMethodValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    VaultCreateBuilder builderFromTx{initialTx.getSTTx()};

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

    // Verify optional fields
    {
        auto const& expected = assetsMaximumValue;
        auto const actualOpt = rebuiltTx.getAssetsMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetsMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfAssetsMaximum");
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
        auto const& expected = withdrawalPolicyValue;
        auto const actualOpt = rebuiltTx.getWithdrawalPolicy();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWithdrawalPolicy should be present";
        expectEqualField(expected, *actualOpt, "sfWithdrawalPolicy");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
    }

    {
        auto const& expected = scaleValue;
        auto const actualOpt = rebuiltTx.getScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfScale should be present";
        expectEqualField(expected, *actualOpt, "sfScale");
    }

    {
        auto const& expected = vaultKindValue;
        auto const actualOpt = rebuiltTx.getVaultKind();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfVaultKind should be present";
        expectEqualField(expected, *actualOpt, "sfVaultKind");
    }

    {
        auto const& expected = subscriptionDateValue;
        auto const actualOpt = rebuiltTx.getSubscriptionDate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubscriptionDate should be present";
        expectEqualField(expected, *actualOpt, "sfSubscriptionDate");
    }

    {
        auto const& expected = redemptionDateValue;
        auto const actualOpt = rebuiltTx.getRedemptionDate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRedemptionDate should be present";
        expectEqualField(expected, *actualOpt, "sfRedemptionDate");
    }

    {
        auto const& expected = dealingIntervalValue;
        auto const actualOpt = rebuiltTx.getDealingInterval();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDealingInterval should be present";
        expectEqualField(expected, *actualOpt, "sfDealingInterval");
    }

    {
        auto const& expected = dealingWindowValue;
        auto const actualOpt = rebuiltTx.getDealingWindow();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDealingWindow should be present";
        expectEqualField(expected, *actualOpt, "sfDealingWindow");
    }

    {
        auto const& expected = depositFeeValue;
        auto const actualOpt = rebuiltTx.getDepositFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDepositFee should be present";
        expectEqualField(expected, *actualOpt, "sfDepositFee");
    }

    {
        auto const& expected = redemptionFeeValue;
        auto const actualOpt = rebuiltTx.getRedemptionFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRedemptionFee should be present";
        expectEqualField(expected, *actualOpt, "sfRedemptionFee");
    }

    {
        auto const& expected = redemptionPeriodValue;
        auto const actualOpt = rebuiltTx.getRedemptionPeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRedemptionPeriod should be present";
        expectEqualField(expected, *actualOpt, "sfRedemptionPeriod");
    }

    {
        auto const& expected = accountingMethodValue;
        auto const actualOpt = rebuiltTx.getAccountingMethod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAccountingMethod should be present";
        expectEqualField(expected, *actualOpt, "sfAccountingMethod");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsVaultCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsVaultCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsVaultCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();

    VaultCreateBuilder builder{
        accountValue,
        assetValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAssetsMaximum());
    EXPECT_FALSE(tx.getAssetsMaximum().has_value());
    EXPECT_FALSE(tx.hasMPTokenMetadata());
    EXPECT_FALSE(tx.getMPTokenMetadata().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
    EXPECT_FALSE(tx.hasWithdrawalPolicy());
    EXPECT_FALSE(tx.getWithdrawalPolicy().has_value());
    EXPECT_FALSE(tx.hasData());
    EXPECT_FALSE(tx.getData().has_value());
    EXPECT_FALSE(tx.hasScale());
    EXPECT_FALSE(tx.getScale().has_value());
    EXPECT_FALSE(tx.hasVaultKind());
    EXPECT_FALSE(tx.getVaultKind().has_value());
    EXPECT_FALSE(tx.hasSubscriptionDate());
    EXPECT_FALSE(tx.getSubscriptionDate().has_value());
    EXPECT_FALSE(tx.hasRedemptionDate());
    EXPECT_FALSE(tx.getRedemptionDate().has_value());
    EXPECT_FALSE(tx.hasDealingInterval());
    EXPECT_FALSE(tx.getDealingInterval().has_value());
    EXPECT_FALSE(tx.hasDealingWindow());
    EXPECT_FALSE(tx.getDealingWindow().has_value());
    EXPECT_FALSE(tx.hasDepositFee());
    EXPECT_FALSE(tx.getDepositFee().has_value());
    EXPECT_FALSE(tx.hasRedemptionFee());
    EXPECT_FALSE(tx.getRedemptionFee().has_value());
    EXPECT_FALSE(tx.hasRedemptionPeriod());
    EXPECT_FALSE(tx.getRedemptionPeriod().has_value());
    EXPECT_FALSE(tx.hasAccountingMethod());
    EXPECT_FALSE(tx.getAccountingMethod().has_value());
}

}
