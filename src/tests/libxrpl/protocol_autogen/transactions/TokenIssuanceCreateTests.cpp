// Auto-generated unit tests for transaction TokenIssuanceCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/TokenIssuanceCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsTokenIssuanceCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTokenIssuanceCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const currencyValue = canonical_CURRENCY();
    auto const maximumAmountValue = canonical_UINT64();
    auto const tokenScaleValue = canonical_UINT8();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const transferFeeValue = canonical_UINT16();
    auto const mPTokenMetadataValue = canonical_VL();

    TokenIssuanceCreateBuilder builder{
        accountValue,
        currencyValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setMaximumAmount(maximumAmountValue);
    builder.setTokenScale(tokenScaleValue);
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
        auto const& expected = maximumAmountValue;
        auto const actualOpt = tx.getMaximumAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaximumAmount should be present";
        expectEqualField(expected, *actualOpt, "sfMaximumAmount");
        EXPECT_TRUE(tx.hasMaximumAmount());
    }

    {
        auto const& expected = tokenScaleValue;
        auto const actualOpt = tx.getTokenScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTokenScale should be present";
        expectEqualField(expected, *actualOpt, "sfTokenScale");
        EXPECT_TRUE(tx.hasTokenScale());
    }

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
TEST(TransactionsTokenIssuanceCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTokenIssuanceCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const currencyValue = canonical_CURRENCY();
    auto const maximumAmountValue = canonical_UINT64();
    auto const tokenScaleValue = canonical_UINT8();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const transferFeeValue = canonical_UINT16();
    auto const mPTokenMetadataValue = canonical_VL();

    // Build an initial transaction
    TokenIssuanceCreateBuilder initialBuilder{
        accountValue,
        currencyValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setMaximumAmount(maximumAmountValue);
    initialBuilder.setTokenScale(tokenScaleValue);
    initialBuilder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);
    initialBuilder.setTransferFee(transferFeeValue);
    initialBuilder.setMPTokenMetadata(mPTokenMetadataValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    TokenIssuanceCreateBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = maximumAmountValue;
        auto const actualOpt = rebuiltTx.getMaximumAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMaximumAmount should be present";
        expectEqualField(expected, *actualOpt, "sfMaximumAmount");
    }

    {
        auto const& expected = tokenScaleValue;
        auto const actualOpt = rebuiltTx.getTokenScale();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTokenScale should be present";
        expectEqualField(expected, *actualOpt, "sfTokenScale");
    }

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
TEST(TransactionsTokenIssuanceCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TokenIssuanceCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsTokenIssuanceCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(TokenIssuanceCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsTokenIssuanceCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testTokenIssuanceCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const currencyValue = canonical_CURRENCY();

    TokenIssuanceCreateBuilder builder{
        accountValue,
        currencyValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasMaximumAmount());
    EXPECT_FALSE(tx.getMaximumAmount().has_value());
    EXPECT_FALSE(tx.hasTokenScale());
    EXPECT_FALSE(tx.getTokenScale().has_value());
    EXPECT_FALSE(tx.hasMPTokenIssuanceID());
    EXPECT_FALSE(tx.getMPTokenIssuanceID().has_value());
    EXPECT_FALSE(tx.hasTransferFee());
    EXPECT_FALSE(tx.getTransferFee().has_value());
    EXPECT_FALSE(tx.hasMPTokenMetadata());
    EXPECT_FALSE(tx.getMPTokenMetadata().has_value());
}

}
