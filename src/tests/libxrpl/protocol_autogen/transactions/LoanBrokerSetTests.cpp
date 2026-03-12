// Auto-generated unit tests for transaction LoanBrokerSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LoanBrokerSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLoanBrokerSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const dataValue = canonical_VL();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const debtMaximumValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();

    LoanBrokerSetBuilder builder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setLoanBrokerID(loanBrokerIDValue);
    builder.setData(dataValue);
    builder.setManagementFeeRate(managementFeeRateValue);
    builder.setDebtMaximum(debtMaximumValue);
    builder.setCoverRateMinimum(coverRateMinimumValue);
    builder.setCoverRateLiquidation(coverRateLiquidationValue);

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
        auto const& expected = loanBrokerIDValue;
        auto const actualOpt = tx.getLoanBrokerID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanBrokerID should be present";
        expectEqualField(expected, *actualOpt, "sfLoanBrokerID");
        EXPECT_TRUE(tx.hasLoanBrokerID());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = tx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx.hasData());
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actualOpt = tx.getManagementFeeRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfManagementFeeRate should be present";
        expectEqualField(expected, *actualOpt, "sfManagementFeeRate");
        EXPECT_TRUE(tx.hasManagementFeeRate());
    }

    {
        auto const& expected = debtMaximumValue;
        auto const actualOpt = tx.getDebtMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDebtMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfDebtMaximum");
        EXPECT_TRUE(tx.hasDebtMaximum());
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

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsLoanBrokerSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const dataValue = canonical_VL();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const debtMaximumValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();

    // Build an initial transaction
    LoanBrokerSetBuilder initialBuilder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setLoanBrokerID(loanBrokerIDValue);
    initialBuilder.setData(dataValue);
    initialBuilder.setManagementFeeRate(managementFeeRateValue);
    initialBuilder.setDebtMaximum(debtMaximumValue);
    initialBuilder.setCoverRateMinimum(coverRateMinimumValue);
    initialBuilder.setCoverRateLiquidation(coverRateLiquidationValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LoanBrokerSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = loanBrokerIDValue;
        auto const actualOpt = rebuiltTx.getLoanBrokerID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanBrokerID should be present";
        expectEqualField(expected, *actualOpt, "sfLoanBrokerID");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actualOpt = rebuiltTx.getManagementFeeRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfManagementFeeRate should be present";
        expectEqualField(expected, *actualOpt, "sfManagementFeeRate");
    }

    {
        auto const& expected = debtMaximumValue;
        auto const actualOpt = rebuiltTx.getDebtMaximum();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDebtMaximum should be present";
        expectEqualField(expected, *actualOpt, "sfDebtMaximum");
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

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsLoanBrokerSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(LoanBrokerSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsLoanBrokerSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(LoanBrokerSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsLoanBrokerSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanBrokerSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const vaultIDValue = canonical_UINT256();

    LoanBrokerSetBuilder builder{
        accountValue,
        vaultIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasLoanBrokerID());
    EXPECT_FALSE(tx.getLoanBrokerID().has_value());
    EXPECT_FALSE(tx.hasData());
    EXPECT_FALSE(tx.getData().has_value());
    EXPECT_FALSE(tx.hasManagementFeeRate());
    EXPECT_FALSE(tx.getManagementFeeRate().has_value());
    EXPECT_FALSE(tx.hasDebtMaximum());
    EXPECT_FALSE(tx.getDebtMaximum().has_value());
    EXPECT_FALSE(tx.hasCoverRateMinimum());
    EXPECT_FALSE(tx.getCoverRateMinimum().has_value());
    EXPECT_FALSE(tx.hasCoverRateLiquidation());
    EXPECT_FALSE(tx.getCoverRateLiquidation().has_value());
}

}
