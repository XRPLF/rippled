// Auto-generated unit tests for transaction TokenIssuanceSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/TokenIssuanceSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsTokenIssuanceSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTokenIssuanceSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const currencyValue = canonical_CURRENCY();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const transferFeeValue = canonical_UINT16();
    auto const mPTokenMetadataValue = canonical_VL();

    TokenIssuanceSetBuilder builder{
        accountValue,
        currencyValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    builder.setTransferFee(transferFeeValue);
    builder.setMPTokenMetadata(mPTokenMetadataValue);

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
        auto const& expected = currencyValue;
        auto const actual = tx.getCurrency();
        expectEqualField(expected, actual, "sfCurrency");
    }

    // Verify optional fields
    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = tx.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenIssuanceID should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
        EXPECT_TRUE(tx.hasMPTokenIssuanceID());
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = tx.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(tx.hasTransferFee());
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = tx.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenMetadata should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
        EXPECT_TRUE(tx.hasMPTokenMetadata());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsTokenIssuanceSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTokenIssuanceSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const currencyValue = canonical_CURRENCY();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const transferFeeValue = canonical_UINT16();
    auto const mPTokenMetadataValue = canonical_VL();

    // Build an initial transaction
    TokenIssuanceSetBuilder initialBuilder{
        accountValue,
        currencyValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    initialBuilder.setTransferFee(transferFeeValue);
    initialBuilder.setMPTokenMetadata(mPTokenMetadataValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    TokenIssuanceSetBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = currencyValue;
        auto const actual = rebuiltTx.getCurrency();
        expectEqualField(expected, actual, "sfCurrency");
    }

    // Verify optional fields
    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = rebuiltTx.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenIssuanceID should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = rebuiltTx.getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
    }

    {
        auto const& expected = mPTokenMetadataValue;
        auto const actualOpt = rebuiltTx.getMPTokenMetadata();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMPTokenMetadata should be present";
        expectEqualField(expected, *actualOpt, "sfMPTokenMetadata");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsTokenIssuanceSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TokenIssuanceSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsTokenIssuanceSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TokenIssuanceSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsTokenIssuanceSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTokenIssuanceSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const currencyValue = canonical_CURRENCY();

    TokenIssuanceSetBuilder builder{
        accountValue,
        currencyValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasMPTokenIssuanceID());
    EXPECT_FALSE(tx.getMPTokenIssuanceID().has_value());
    EXPECT_FALSE(tx.hasTransferFee());
    EXPECT_FALSE(tx.getTransferFee().has_value());
    EXPECT_FALSE(tx.hasMPTokenMetadata());
    EXPECT_FALSE(tx.getMPTokenMetadata().has_value());
}

}
