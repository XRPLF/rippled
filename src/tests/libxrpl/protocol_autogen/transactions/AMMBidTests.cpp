// Auto-generated unit tests for transaction AMMBid


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMBid.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAMMBidTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMBid"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const bidMinValue = canonical_AMOUNT();
    auto const bidMaxValue = canonical_AMOUNT();
    auto const authAccountsValue = canonical_ARRAY();

    AMMBidBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setBidMin(bidMinValue);
    builder.setBidMax(bidMaxValue);
    builder.setAuthAccounts(authAccountsValue);

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
        auto const& expected = assetValue;
        auto const actual = tx.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = tx.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = bidMinValue;
        auto const actualOpt = tx.getBidMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMin should be present";
        expectEqualField(expected, *actualOpt, "sfBidMin");
        EXPECT_TRUE(tx.hasBidMin());
    }

    {
        auto const& expected = bidMaxValue;
        auto const actualOpt = tx.getBidMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMax should be present";
        expectEqualField(expected, *actualOpt, "sfBidMax");
        EXPECT_TRUE(tx.hasBidMax());
    }

    {
        auto const& expected = authAccountsValue;
        auto const actualOpt = tx.getAuthAccounts();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthAccounts should be present";
        expectEqualField(expected, *actualOpt, "sfAuthAccounts");
        EXPECT_TRUE(tx.hasAuthAccounts());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAMMBidTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMBidFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const bidMinValue = canonical_AMOUNT();
    auto const bidMaxValue = canonical_AMOUNT();
    auto const authAccountsValue = canonical_ARRAY();

    // Build an initial transaction
    AMMBidBuilder initialBuilder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    initialBuilder.setBidMin(bidMinValue);
    initialBuilder.setBidMax(bidMaxValue);
    initialBuilder.setAuthAccounts(authAccountsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AMMBidBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = assetValue;
        auto const actual = rebuiltTx.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = rebuiltTx.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = bidMinValue;
        auto const actualOpt = rebuiltTx.getBidMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMin should be present";
        expectEqualField(expected, *actualOpt, "sfBidMin");
    }

    {
        auto const& expected = bidMaxValue;
        auto const actualOpt = rebuiltTx.getBidMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMax should be present";
        expectEqualField(expected, *actualOpt, "sfBidMax");
    }

    {
        auto const& expected = authAccountsValue;
        auto const actualOpt = rebuiltTx.getAuthAccounts();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthAccounts should be present";
        expectEqualField(expected, *actualOpt, "sfAuthAccounts");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAMMBidTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMBid{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAMMBidTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AMMBidBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAMMBidTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAMMBidNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();

    AMMBidBuilder builder{
        accountValue,
        assetValue,
        asset2Value,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasBidMin());
    EXPECT_FALSE(tx.getBidMin().has_value());
    EXPECT_FALSE(tx.hasBidMax());
    EXPECT_FALSE(tx.getBidMax().has_value());
    EXPECT_FALSE(tx.hasAuthAccounts());
    EXPECT_FALSE(tx.getAuthAccounts().has_value());
}

}
