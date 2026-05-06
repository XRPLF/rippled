// Auto-generated unit tests for transaction SponsorshipSet

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>
#include <xrpl/protocol_autogen/transactions/SponsorshipSet.h>

#include <gtest/gtest.h>
#include <protocol_autogen/TestHelpers.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsSponsorshipSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSponsorshipSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartySponsorValue = canonical_ACCOUNT();
    auto const sponseeValue = canonical_ACCOUNT();
    auto const feeAmountValue = canonical_AMOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const reserveCountValue = canonical_UINT32();

    SponsorshipSetBuilder builder{accountValue, sequenceValue, feeValue};

    // Set optional fields
    builder.setCounterpartySponsor(counterpartySponsorValue);
    builder.setSponsee(sponseeValue);
    builder.setFeeAmount(feeAmountValue);
    builder.setMaxFee(maxFeeValue);
    builder.setReserveCount(reserveCountValue);

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
        auto const& expected = counterpartySponsorValue;
        auto const actualOpt = tx.getCounterpartySponsor();
        ASSERT_TRUE(actualOpt.has_value())
            << "Optional field sfCounterpartySponsor should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySponsor");
        EXPECT_TRUE(tx.hasCounterpartySponsor());
    }

    {
        auto const& expected = sponseeValue;
        auto const actualOpt = tx.getSponsee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSponsee should be present";
        expectEqualField(expected, *actualOpt, "sfSponsee");
        EXPECT_TRUE(tx.hasSponsee());
    }

    {
        auto const& expected = feeAmountValue;
        auto const actualOpt = tx.getFeeAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFeeAmount should be present";
        expectEqualField(expected, *actualOpt, "sfFeeAmount");
        EXPECT_TRUE(tx.hasFeeAmount());
    }

    {
        auto const& expected = maxFeeValue;
        auto const actualOpt = tx.getMaxFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaxFee should be present";
        expectEqualField(expected, *actualOpt, "sfMaxFee");
        EXPECT_TRUE(tx.hasMaxFee());
    }

    {
        auto const& expected = reserveCountValue;
        auto const actualOpt = tx.getReserveCount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveCount should be present";
        expectEqualField(expected, *actualOpt, "sfReserveCount");
        EXPECT_TRUE(tx.hasReserveCount());
    }
}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsSponsorshipSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSponsorshipSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartySponsorValue = canonical_ACCOUNT();
    auto const sponseeValue = canonical_ACCOUNT();
    auto const feeAmountValue = canonical_AMOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const reserveCountValue = canonical_UINT32();

    // Build an initial transaction
    SponsorshipSetBuilder initialBuilder{accountValue, sequenceValue, feeValue};

    initialBuilder.setCounterpartySponsor(counterpartySponsorValue);
    initialBuilder.setSponsee(sponseeValue);
    initialBuilder.setFeeAmount(feeAmountValue);
    initialBuilder.setMaxFee(maxFeeValue);
    initialBuilder.setReserveCount(reserveCountValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    SponsorshipSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = counterpartySponsorValue;
        auto const actualOpt = rebuiltTx.getCounterpartySponsor();
        ASSERT_TRUE(actualOpt.has_value())
            << "Optional field sfCounterpartySponsor should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySponsor");
    }

    {
        auto const& expected = sponseeValue;
        auto const actualOpt = rebuiltTx.getSponsee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSponsee should be present";
        expectEqualField(expected, *actualOpt, "sfSponsee");
    }

    {
        auto const& expected = feeAmountValue;
        auto const actualOpt = rebuiltTx.getFeeAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFeeAmount should be present";
        expectEqualField(expected, *actualOpt, "sfFeeAmount");
    }

    {
        auto const& expected = maxFeeValue;
        auto const actualOpt = rebuiltTx.getMaxFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaxFee should be present";
        expectEqualField(expected, *actualOpt, "sfMaxFee");
    }

    {
        auto const& expected = reserveCountValue;
        auto const actualOpt = rebuiltTx.getReserveCount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfReserveCount should be present";
        expectEqualField(expected, *actualOpt, "sfReserveCount");
    }
}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsSponsorshipSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] = generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SponsorshipSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsSponsorshipSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] = generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SponsorshipSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsSponsorshipSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSponsorshipSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    SponsorshipSetBuilder builder{accountValue, sequenceValue, feeValue};

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasCounterpartySponsor());
    EXPECT_FALSE(tx.getCounterpartySponsor().has_value());
    EXPECT_FALSE(tx.hasSponsee());
    EXPECT_FALSE(tx.getSponsee().has_value());
    EXPECT_FALSE(tx.hasFeeAmount());
    EXPECT_FALSE(tx.getFeeAmount().has_value());
    EXPECT_FALSE(tx.hasMaxFee());
    EXPECT_FALSE(tx.getMaxFee().has_value());
    EXPECT_FALSE(tx.hasReserveCount());
    EXPECT_FALSE(tx.getReserveCount().has_value());
}

}  // namespace xrpl::transactions
