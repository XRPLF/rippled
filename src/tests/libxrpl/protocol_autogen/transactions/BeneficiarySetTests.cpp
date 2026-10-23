// Auto-generated unit tests for transaction BeneficiarySet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/BeneficiarySet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsBeneficiarySetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBeneficiarySet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const beneficiaryValue = canonical_ACCOUNT();
    auto const timeLockValue = canonical_UINT32();

    BeneficiarySetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setBeneficiary(beneficiaryValue);
    builder.setTimeLock(timeLockValue);

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
        auto const& expected = beneficiaryValue;
        auto const actualOpt = tx.getBeneficiary();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBeneficiary should be present";
        expectEqualField(expected, *actualOpt, "sfBeneficiary");
        EXPECT_TRUE(tx.hasBeneficiary());
    }

    {
        auto const& expected = timeLockValue;
        auto const actualOpt = tx.getTimeLock();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTimeLock should be present";
        expectEqualField(expected, *actualOpt, "sfTimeLock");
        EXPECT_TRUE(tx.hasTimeLock());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsBeneficiarySetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBeneficiarySetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const beneficiaryValue = canonical_ACCOUNT();
    auto const timeLockValue = canonical_UINT32();

    // Build an initial transaction
    BeneficiarySetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setBeneficiary(beneficiaryValue);
    initialBuilder.setTimeLock(timeLockValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    BeneficiarySetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = beneficiaryValue;
        auto const actualOpt = rebuiltTx.getBeneficiary();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBeneficiary should be present";
        expectEqualField(expected, *actualOpt, "sfBeneficiary");
    }

    {
        auto const& expected = timeLockValue;
        auto const actualOpt = rebuiltTx.getTimeLock();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTimeLock should be present";
        expectEqualField(expected, *actualOpt, "sfTimeLock");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsBeneficiarySetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BeneficiarySet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsBeneficiarySetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(BeneficiarySetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsBeneficiarySetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testBeneficiarySetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    BeneficiarySetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasBeneficiary());
    EXPECT_FALSE(tx.getBeneficiary().has_value());
    EXPECT_FALSE(tx.hasTimeLock());
    EXPECT_FALSE(tx.getTimeLock().has_value());
}

}
