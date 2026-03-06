// Auto-generated unit tests for transaction AMMBid

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AMMBid.h>

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

    std::string reason;
    EXPECT_TRUE(builder.validate(reason)) << reason;

    auto tx = builder.build(publicKey, secretKey);

    EXPECT_TRUE(tx->validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx->getSigningPubKey().empty());
    EXPECT_TRUE(tx->hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx->getAccount(), accountValue);
    EXPECT_EQ(tx->getSequence(), sequenceValue);
    EXPECT_EQ(tx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = assetValue;
        auto const actual = tx->getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = tx->getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = bidMinValue;
        auto const actualOpt = tx->getBidMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMin should be present";
        expectEqualField(expected, *actualOpt, "sfBidMin");
        EXPECT_TRUE(tx->hasBidMin());
    }

    {
        auto const& expected = bidMaxValue;
        auto const actualOpt = tx->getBidMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMax should be present";
        expectEqualField(expected, *actualOpt, "sfBidMax");
        EXPECT_TRUE(tx->hasBidMax());
    }

    {
        auto const& expected = authAccountsValue;
        auto const actualOpt = tx->getAuthAccounts();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthAccounts should be present";
        expectEqualField(expected, *actualOpt, "sfAuthAccounts");
        EXPECT_TRUE(tx->hasAuthAccounts());
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
    AMMBidBuilder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = assetValue;
        auto const actual = rebuiltTx->getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = rebuiltTx->getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    // Verify optional fields
    {
        auto const& expected = bidMinValue;
        auto const actualOpt = rebuiltTx->getBidMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMin should be present";
        expectEqualField(expected, *actualOpt, "sfBidMin");
    }

    {
        auto const& expected = bidMaxValue;
        auto const actualOpt = rebuiltTx->getBidMax();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfBidMax should be present";
        expectEqualField(expected, *actualOpt, "sfBidMax");
    }

    {
        auto const& expected = authAccountsValue;
        auto const actualOpt = rebuiltTx->getAuthAccounts();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthAccounts should be present";
        expectEqualField(expected, *actualOpt, "sfAuthAccounts");
    }

}

}
