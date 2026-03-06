// Auto-generated unit tests for transaction MPTokenAuthorize

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/MPTokenAuthorize.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsMPTokenAuthorizeTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenAuthorize"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderValue = canonical_ACCOUNT();

    MPTokenAuthorizeBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setHolder(holderValue);

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
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = tx->getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    // Verify optional fields
    {
        auto const& expected = holderValue;
        auto const actualOpt = tx->getHolder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolder should be present";
        expectEqualField(expected, *actualOpt, "sfHolder");
        EXPECT_TRUE(tx->hasHolder());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsMPTokenAuthorizeTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testMPTokenAuthorizeFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderValue = canonical_ACCOUNT();

    // Build an initial transaction
    MPTokenAuthorizeBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setHolder(holderValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    MPTokenAuthorizeBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = rebuiltTx->getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    // Verify optional fields
    {
        auto const& expected = holderValue;
        auto const actualOpt = rebuiltTx->getHolder();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfHolder should be present";
        expectEqualField(expected, *actualOpt, "sfHolder");
    }

}

}
