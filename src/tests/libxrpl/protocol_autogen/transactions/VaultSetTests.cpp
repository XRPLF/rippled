// Auto-generated unit tests for transaction VaultSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/VaultSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsVaultSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const assetsMaximumValue = canonical_NUMBER();
    auto const domainIDValue = canonical_UINT256();
    auto const dataValue = canonical_VL();
    auto const depositFeeValue = canonical_UINT32();
    auto const redemptionFeeValue = canonical_UINT32();
    auto const redemptionPeriodValue = canonical_UINT32();

    VaultSetBuilder builder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAssetsMaximum(assetsMaximumValue);
    builder.setDomainID(domainIDValue);
    builder.setData(dataValue);
    builder.setDepositFee(depositFeeValue);
    builder.setRedemptionFee(redemptionFeeValue);
    builder.setRedemptionPeriod(redemptionPeriodValue);

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
        auto const& expected = vaultIDValue;
        auto const actual = tx.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
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
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = tx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx.hasData());
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

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsVaultSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const assetsMaximumValue = canonical_NUMBER();
    auto const domainIDValue = canonical_UINT256();
    auto const dataValue = canonical_VL();
    auto const depositFeeValue = canonical_UINT32();
    auto const redemptionFeeValue = canonical_UINT32();
    auto const redemptionPeriodValue = canonical_UINT32();

    // Build an initial transaction
    VaultSetBuilder initialBuilder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAssetsMaximum(assetsMaximumValue);
    initialBuilder.setDomainID(domainIDValue);
    initialBuilder.setData(dataValue);
    initialBuilder.setDepositFee(depositFeeValue);
    initialBuilder.setRedemptionFee(redemptionFeeValue);
    initialBuilder.setRedemptionPeriod(redemptionPeriodValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    VaultSetBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = vaultIDValue;
        auto const actual = rebuiltTx.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
    }

    // Verify optional fields
    {
        auto const& expected = assetsMaximumValue;
        auto const actualOpt = rebuiltTx.getAssetsMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAssetsMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfAssetsMaximum");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
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

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsVaultSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsVaultSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsVaultSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const vaultIDValue = canonical_UINT256();

    VaultSetBuilder builder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAssetsMaximum());
    EXPECT_FALSE(tx.getAssetsMaximum().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
    EXPECT_FALSE(tx.hasData());
    EXPECT_FALSE(tx.getData().has_value());
    EXPECT_FALSE(tx.hasDepositFee());
    EXPECT_FALSE(tx.getDepositFee().has_value());
    EXPECT_FALSE(tx.hasRedemptionFee());
    EXPECT_FALSE(tx.getRedemptionFee().has_value());
    EXPECT_FALSE(tx.hasRedemptionPeriod());
    EXPECT_FALSE(tx.getRedemptionPeriod().has_value());
}

}
