// Auto-generated unit tests for transaction PerpLiquidate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/PerpLiquidate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsPerpLiquidateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpLiquidate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const sizeValue = canonical_NUMBER();

    PerpLiquidateBuilder builder{
        accountValue,
        marginAccountIDValue,
        marketIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setSize(sizeValue);

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
        auto const& expected = marginAccountIDValue;
        auto const actual = tx.getMarginAccountID();
        expectEqualField(expected, actual, "sfMarginAccountID");
    }

    {
        auto const& expected = marketIDValue;
        auto const actual = tx.getMarketID();
        expectEqualField(expected, actual, "sfMarketID");
    }

    // Verify optional fields
    {
        auto const& expected = sizeValue;
        auto const actualOpt = tx.getSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSize should be present";
        expectEqualField(expected, *actualOpt, "sfSize");
        EXPECT_TRUE(tx.hasSize());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsPerpLiquidateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpLiquidateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const sizeValue = canonical_NUMBER();

    // Build an initial transaction
    PerpLiquidateBuilder initialBuilder{
        accountValue,
        marginAccountIDValue,
        marketIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setSize(sizeValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    PerpLiquidateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = marginAccountIDValue;
        auto const actual = rebuiltTx.getMarginAccountID();
        expectEqualField(expected, actual, "sfMarginAccountID");
    }

    {
        auto const& expected = marketIDValue;
        auto const actual = rebuiltTx.getMarketID();
        expectEqualField(expected, actual, "sfMarketID");
    }

    // Verify optional fields
    {
        auto const& expected = sizeValue;
        auto const actualOpt = rebuiltTx.getSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSize should be present";
        expectEqualField(expected, *actualOpt, "sfSize");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsPerpLiquidateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpLiquidate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsPerpLiquidateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(PerpLiquidateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsPerpLiquidateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testPerpLiquidateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();

    PerpLiquidateBuilder builder{
        accountValue,
        marginAccountIDValue,
        marketIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasSize());
    EXPECT_FALSE(tx.getSize().has_value());
}

}
