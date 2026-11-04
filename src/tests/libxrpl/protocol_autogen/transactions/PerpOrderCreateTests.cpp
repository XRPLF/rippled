// Auto-generated unit tests for transaction PerpOrderCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/PerpOrderCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPerpOrderCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpOrderCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const marketIDValue = canonical_UINT256();
    auto const subaccountValue = canonical_UINT32();
    auto const limitPriceValue = canonical_NUMBER();
    auto const sizeValue = canonical_NUMBER();
    auto const offerSequenceValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const builderValue = canonical_ACCOUNT();
    auto const builderFeeValue = canonical_UINT32();

    PerpOrderCreateBuilder builder{
        accountValue,
        marketIDValue,
        limitPriceValue,
        sizeValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setSubaccount(subaccountValue);
    builder.setOfferSequence(offerSequenceValue);
    builder.setExpiration(expirationValue);
    builder.setBuilder(builderValue);
    builder.setBuilderFee(builderFeeValue);

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
        auto const& expected = marketIDValue;
        auto const actual = tx.getMarketID();
        expectEqualField(expected, actual, "sfMarketID");
    }

    {
        auto const& expected = limitPriceValue;
        auto const actual = tx.getLimitPrice();
        expectEqualField(expected, actual, "sfLimitPrice");
    }

    {
        auto const& expected = sizeValue;
        auto const actual = tx.getSize();
        expectEqualField(expected, actual, "sfSize");
    }

    // Verify optional fields
    {
        auto const& expected = subaccountValue;
        auto const actualOpt = tx.getSubaccount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubaccount should be present";
        expectEqualField(expected, *actualOpt, "sfSubaccount");
        EXPECT_TRUE(tx.hasSubaccount());
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actualOpt = tx.getOfferSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOfferSequence should be present";
        expectEqualField(expected, *actualOpt, "sfOfferSequence");
        EXPECT_TRUE(tx.hasOfferSequence());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx.hasExpiration());
    }

    {
        auto const& expected = builderValue;
        auto const actualOpt = tx.getBuilder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBuilder should be present";
        expectEqualField(expected, *actualOpt, "sfBuilder");
        EXPECT_TRUE(tx.hasBuilder());
    }

    {
        auto const& expected = builderFeeValue;
        auto const actualOpt = tx.getBuilderFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBuilderFee should be present";
        expectEqualField(expected, *actualOpt, "sfBuilderFee");
        EXPECT_TRUE(tx.hasBuilderFee());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPerpOrderCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpOrderCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const marketIDValue = canonical_UINT256();
    auto const subaccountValue = canonical_UINT32();
    auto const limitPriceValue = canonical_NUMBER();
    auto const sizeValue = canonical_NUMBER();
    auto const offerSequenceValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const builderValue = canonical_ACCOUNT();
    auto const builderFeeValue = canonical_UINT32();

    // Build an initial transaction
    PerpOrderCreateBuilder initialBuilder{
        accountValue,
        marketIDValue,
        limitPriceValue,
        sizeValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setSubaccount(subaccountValue);
    initialBuilder.setOfferSequence(offerSequenceValue);
    initialBuilder.setExpiration(expirationValue);
    initialBuilder.setBuilder(builderValue);
    initialBuilder.setBuilderFee(builderFeeValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PerpOrderCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = marketIDValue;
        auto const actual = rebuiltTx.getMarketID();
        expectEqualField(expected, actual, "sfMarketID");
    }

    {
        auto const& expected = limitPriceValue;
        auto const actual = rebuiltTx.getLimitPrice();
        expectEqualField(expected, actual, "sfLimitPrice");
    }

    {
        auto const& expected = sizeValue;
        auto const actual = rebuiltTx.getSize();
        expectEqualField(expected, actual, "sfSize");
    }

    // Verify optional fields
    {
        auto const& expected = subaccountValue;
        auto const actualOpt = rebuiltTx.getSubaccount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubaccount should be present";
        expectEqualField(expected, *actualOpt, "sfSubaccount");
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actualOpt = rebuiltTx.getOfferSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOfferSequence should be present";
        expectEqualField(expected, *actualOpt, "sfOfferSequence");
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

    {
        auto const& expected = builderValue;
        auto const actualOpt = rebuiltTx.getBuilder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBuilder should be present";
        expectEqualField(expected, *actualOpt, "sfBuilder");
    }

    {
        auto const& expected = builderFeeValue;
        auto const actualOpt = rebuiltTx.getBuilderFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBuilderFee should be present";
        expectEqualField(expected, *actualOpt, "sfBuilderFee");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPerpOrderCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpOrderCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPerpOrderCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpOrderCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPerpOrderCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpOrderCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const marketIDValue = canonical_UINT256();
    auto const limitPriceValue = canonical_NUMBER();
    auto const sizeValue = canonical_NUMBER();

    PerpOrderCreateBuilder builder{
        accountValue,
        marketIDValue,
        limitPriceValue,
        sizeValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasSubaccount());
    EXPECT_FALSE(tx.getSubaccount().has_value());
    EXPECT_FALSE(tx.hasOfferSequence());
    EXPECT_FALSE(tx.getOfferSequence().has_value());
    EXPECT_FALSE(tx.hasExpiration());
    EXPECT_FALSE(tx.getExpiration().has_value());
    EXPECT_FALSE(tx.hasBuilder());
    EXPECT_FALSE(tx.getBuilder().has_value());
    EXPECT_FALSE(tx.hasBuilderFee());
    EXPECT_FALSE(tx.getBuilderFee().has_value());
}

}
