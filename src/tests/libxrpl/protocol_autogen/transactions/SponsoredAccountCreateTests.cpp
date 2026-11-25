// Auto-generated unit tests for transaction SponsoredAccountCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/SponsoredAccountCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsSponsoredAccountCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSponsoredAccountCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const feeAmountValue = canonical_AMOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const remainingOwnerCountValue = canonical_UINT32();

    SponsoredAccountCreateBuilder builder{
        accountValue,
        destinationValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setFeeAmount(feeAmountValue);
    builder.setMaxFee(maxFeeValue);
    builder.setRemainingOwnerCount(remainingOwnerCountValue);

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
        auto const& expected = destinationValue;
        auto const actual = tx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    // Verify optional fields
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
        auto const& expected = remainingOwnerCountValue;
        auto const actualOpt = tx.getRemainingOwnerCount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRemainingOwnerCount should be present";
        expectEqualField(expected, *actualOpt, "sfRemainingOwnerCount");
        EXPECT_TRUE(tx.hasRemainingOwnerCount());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsSponsoredAccountCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSponsoredAccountCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const destinationValue = canonical_ACCOUNT();
    auto const feeAmountValue = canonical_AMOUNT();
    auto const maxFeeValue = canonical_AMOUNT();
    auto const remainingOwnerCountValue = canonical_UINT32();

    // Build an initial transaction
    SponsoredAccountCreateBuilder initialBuilder{
        accountValue,
        destinationValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setFeeAmount(feeAmountValue);
    initialBuilder.setMaxFee(maxFeeValue);
    initialBuilder.setRemainingOwnerCount(remainingOwnerCountValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    SponsoredAccountCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = destinationValue;
        auto const actual = rebuiltTx.getDestination();
        expectEqualField(expected, actual, "sfDestination");
    }

    // Verify optional fields
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
        auto const& expected = remainingOwnerCountValue;
        auto const actualOpt = rebuiltTx.getRemainingOwnerCount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfRemainingOwnerCount should be present";
        expectEqualField(expected, *actualOpt, "sfRemainingOwnerCount");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsSponsoredAccountCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SponsoredAccountCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsSponsoredAccountCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SponsoredAccountCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsSponsoredAccountCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testSponsoredAccountCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const destinationValue = canonical_ACCOUNT();

    SponsoredAccountCreateBuilder builder{
        accountValue,
        destinationValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasFeeAmount());
    EXPECT_FALSE(tx.getFeeAmount().has_value());
    EXPECT_FALSE(tx.hasMaxFee());
    EXPECT_FALSE(tx.getMaxFee().has_value());
    EXPECT_FALSE(tx.hasRemainingOwnerCount());
    EXPECT_FALSE(tx.getRemainingOwnerCount().has_value());
}

}
