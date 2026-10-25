// Auto-generated unit tests for transaction VaultManagerSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/VaultManagerSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsVaultManagerSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultManagerSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const managerValue = canonical_ACCOUNT();
    auto const valuerValue = canonical_ACCOUNT();
    auto const guardianValue = canonical_ACCOUNT();
    auto const mandateValue = canonical_ARRAY();
    auto const deployedMaximumValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const performanceFeeRateValue = canonical_UINT32();
    auto const markMoveLimitValue = canonical_UINT32();
    auto const markHaltPeriodValue = canonical_UINT32();
    auto const markStaleAfterValue = canonical_UINT32();
    auto const timelockValue = canonical_UINT32();

    VaultManagerSetBuilder builder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setManager(managerValue);
    builder.setValuer(valuerValue);
    builder.setGuardian(guardianValue);
    builder.setMandate(mandateValue);
    builder.setDeployedMaximum(deployedMaximumValue);
    builder.setCoverRateMinimum(coverRateMinimumValue);
    builder.setCoverRateLiquidation(coverRateLiquidationValue);
    builder.setManagementFeeRate(managementFeeRateValue);
    builder.setPerformanceFeeRate(performanceFeeRateValue);
    builder.setMarkMoveLimit(markMoveLimitValue);
    builder.setMarkHaltPeriod(markHaltPeriodValue);
    builder.setMarkStaleAfter(markStaleAfterValue);
    builder.setTimelock(timelockValue);

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
        auto const& expected = managerValue;
        auto const actualOpt = tx.getManager();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfManager should be present";
        expectEqualField(expected, *actualOpt, "sfManager");
        EXPECT_TRUE(tx.hasManager());
    }

    {
        auto const& expected = valuerValue;
        auto const actualOpt = tx.getValuer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfValuer should be present";
        expectEqualField(expected, *actualOpt, "sfValuer");
        EXPECT_TRUE(tx.hasValuer());
    }

    {
        auto const& expected = guardianValue;
        auto const actualOpt = tx.getGuardian();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfGuardian should be present";
        expectEqualField(expected, *actualOpt, "sfGuardian");
        EXPECT_TRUE(tx.hasGuardian());
    }

    {
        auto const& expected = mandateValue;
        auto const actualOpt = tx.getMandate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMandate should be present";
        expectEqualField(expected, *actualOpt, "sfMandate");
        EXPECT_TRUE(tx.hasMandate());
    }

    {
        auto const& expected = deployedMaximumValue;
        auto const actualOpt = tx.getDeployedMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeployedMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfDeployedMaximum");
        EXPECT_TRUE(tx.hasDeployedMaximum());
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = tx.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCoverRateMinimum should be present";
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
        EXPECT_TRUE(tx.hasCoverRateMinimum());
    }

    {
        auto const& expected = coverRateLiquidationValue;
        auto const actualOpt = tx.getCoverRateLiquidation();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCoverRateLiquidation should be present";
        expectEqualField(expected, *actualOpt, "sfCoverRateLiquidation");
        EXPECT_TRUE(tx.hasCoverRateLiquidation());
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actualOpt = tx.getManagementFeeRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfManagementFeeRate should be present";
        expectEqualField(expected, *actualOpt, "sfManagementFeeRate");
        EXPECT_TRUE(tx.hasManagementFeeRate());
    }

    {
        auto const& expected = performanceFeeRateValue;
        auto const actualOpt = tx.getPerformanceFeeRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPerformanceFeeRate should be present";
        expectEqualField(expected, *actualOpt, "sfPerformanceFeeRate");
        EXPECT_TRUE(tx.hasPerformanceFeeRate());
    }

    {
        auto const& expected = markMoveLimitValue;
        auto const actualOpt = tx.getMarkMoveLimit();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarkMoveLimit should be present";
        expectEqualField(expected, *actualOpt, "sfMarkMoveLimit");
        EXPECT_TRUE(tx.hasMarkMoveLimit());
    }

    {
        auto const& expected = markHaltPeriodValue;
        auto const actualOpt = tx.getMarkHaltPeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarkHaltPeriod should be present";
        expectEqualField(expected, *actualOpt, "sfMarkHaltPeriod");
        EXPECT_TRUE(tx.hasMarkHaltPeriod());
    }

    {
        auto const& expected = markStaleAfterValue;
        auto const actualOpt = tx.getMarkStaleAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarkStaleAfter should be present";
        expectEqualField(expected, *actualOpt, "sfMarkStaleAfter");
        EXPECT_TRUE(tx.hasMarkStaleAfter());
    }

    {
        auto const& expected = timelockValue;
        auto const actualOpt = tx.getTimelock();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTimelock should be present";
        expectEqualField(expected, *actualOpt, "sfTimelock");
        EXPECT_TRUE(tx.hasTimelock());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsVaultManagerSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultManagerSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const managerValue = canonical_ACCOUNT();
    auto const valuerValue = canonical_ACCOUNT();
    auto const guardianValue = canonical_ACCOUNT();
    auto const mandateValue = canonical_ARRAY();
    auto const deployedMaximumValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const performanceFeeRateValue = canonical_UINT32();
    auto const markMoveLimitValue = canonical_UINT32();
    auto const markHaltPeriodValue = canonical_UINT32();
    auto const markStaleAfterValue = canonical_UINT32();
    auto const timelockValue = canonical_UINT32();

    // Build an initial transaction
    VaultManagerSetBuilder initialBuilder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setManager(managerValue);
    initialBuilder.setValuer(valuerValue);
    initialBuilder.setGuardian(guardianValue);
    initialBuilder.setMandate(mandateValue);
    initialBuilder.setDeployedMaximum(deployedMaximumValue);
    initialBuilder.setCoverRateMinimum(coverRateMinimumValue);
    initialBuilder.setCoverRateLiquidation(coverRateLiquidationValue);
    initialBuilder.setManagementFeeRate(managementFeeRateValue);
    initialBuilder.setPerformanceFeeRate(performanceFeeRateValue);
    initialBuilder.setMarkMoveLimit(markMoveLimitValue);
    initialBuilder.setMarkHaltPeriod(markHaltPeriodValue);
    initialBuilder.setMarkStaleAfter(markStaleAfterValue);
    initialBuilder.setTimelock(timelockValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    VaultManagerSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = managerValue;
        auto const actualOpt = rebuiltTx.getManager();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfManager should be present";
        expectEqualField(expected, *actualOpt, "sfManager");
    }

    {
        auto const& expected = valuerValue;
        auto const actualOpt = rebuiltTx.getValuer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfValuer should be present";
        expectEqualField(expected, *actualOpt, "sfValuer");
    }

    {
        auto const& expected = guardianValue;
        auto const actualOpt = rebuiltTx.getGuardian();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfGuardian should be present";
        expectEqualField(expected, *actualOpt, "sfGuardian");
    }

    {
        auto const& expected = mandateValue;
        auto const actualOpt = rebuiltTx.getMandate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMandate should be present";
        expectEqualField(expected, *actualOpt, "sfMandate");
    }

    {
        auto const& expected = deployedMaximumValue;
        auto const actualOpt = rebuiltTx.getDeployedMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeployedMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfDeployedMaximum");
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = rebuiltTx.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCoverRateMinimum should be present";
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
    }

    {
        auto const& expected = coverRateLiquidationValue;
        auto const actualOpt = rebuiltTx.getCoverRateLiquidation();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCoverRateLiquidation should be present";
        expectEqualField(expected, *actualOpt, "sfCoverRateLiquidation");
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actualOpt = rebuiltTx.getManagementFeeRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfManagementFeeRate should be present";
        expectEqualField(expected, *actualOpt, "sfManagementFeeRate");
    }

    {
        auto const& expected = performanceFeeRateValue;
        auto const actualOpt = rebuiltTx.getPerformanceFeeRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPerformanceFeeRate should be present";
        expectEqualField(expected, *actualOpt, "sfPerformanceFeeRate");
    }

    {
        auto const& expected = markMoveLimitValue;
        auto const actualOpt = rebuiltTx.getMarkMoveLimit();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarkMoveLimit should be present";
        expectEqualField(expected, *actualOpt, "sfMarkMoveLimit");
    }

    {
        auto const& expected = markHaltPeriodValue;
        auto const actualOpt = rebuiltTx.getMarkHaltPeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarkHaltPeriod should be present";
        expectEqualField(expected, *actualOpt, "sfMarkHaltPeriod");
    }

    {
        auto const& expected = markStaleAfterValue;
        auto const actualOpt = rebuiltTx.getMarkStaleAfter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMarkStaleAfter should be present";
        expectEqualField(expected, *actualOpt, "sfMarkStaleAfter");
    }

    {
        auto const& expected = timelockValue;
        auto const actualOpt = rebuiltTx.getTimelock();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTimelock should be present";
        expectEqualField(expected, *actualOpt, "sfTimelock");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsVaultManagerSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultManagerSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsVaultManagerSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultManagerSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsVaultManagerSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultManagerSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const vaultIDValue = canonical_UINT256();

    VaultManagerSetBuilder builder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasManager());
    EXPECT_FALSE(tx.getManager().has_value());
    EXPECT_FALSE(tx.hasValuer());
    EXPECT_FALSE(tx.getValuer().has_value());
    EXPECT_FALSE(tx.hasGuardian());
    EXPECT_FALSE(tx.getGuardian().has_value());
    EXPECT_FALSE(tx.hasMandate());
    EXPECT_FALSE(tx.getMandate().has_value());
    EXPECT_FALSE(tx.hasDeployedMaximum());
    EXPECT_FALSE(tx.getDeployedMaximum().has_value());
    EXPECT_FALSE(tx.hasCoverRateMinimum());
    EXPECT_FALSE(tx.getCoverRateMinimum().has_value());
    EXPECT_FALSE(tx.hasCoverRateLiquidation());
    EXPECT_FALSE(tx.getCoverRateLiquidation().has_value());
    EXPECT_FALSE(tx.hasManagementFeeRate());
    EXPECT_FALSE(tx.getManagementFeeRate().has_value());
    EXPECT_FALSE(tx.hasPerformanceFeeRate());
    EXPECT_FALSE(tx.getPerformanceFeeRate().has_value());
    EXPECT_FALSE(tx.hasMarkMoveLimit());
    EXPECT_FALSE(tx.getMarkMoveLimit().has_value());
    EXPECT_FALSE(tx.hasMarkHaltPeriod());
    EXPECT_FALSE(tx.getMarkHaltPeriod().has_value());
    EXPECT_FALSE(tx.hasMarkStaleAfter());
    EXPECT_FALSE(tx.getMarkStaleAfter().has_value());
    EXPECT_FALSE(tx.hasTimelock());
    EXPECT_FALSE(tx.getTimelock().has_value());
}

}
