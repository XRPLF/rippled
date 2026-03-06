// Auto-generated unit tests for transaction OfferCreate

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/OfferCreate.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsOfferCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testOfferCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const takerPaysValue = canonical_AMOUNT();
    auto const takerGetsValue = canonical_AMOUNT();
    auto const expirationValue = canonical_UINT32();
    auto const offerSequenceValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();

    OfferCreateBuilder builder{
        accountValue,
        takerPaysValue,
        takerGetsValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setExpiration(expirationValue);
    builder.setOfferSequence(offerSequenceValue);
    builder.setDomainID(domainIDValue);

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
        auto const& expected = takerPaysValue;
        auto const actual = tx->getTakerPays();
        expectEqualField(expected, actual, "sfTakerPays");
    }

    {
        auto const& expected = takerGetsValue;
        auto const actual = tx->getTakerGets();
        expectEqualField(expected, actual, "sfTakerGets");
    }

    // Verify optional fields
    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx->getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx->hasExpiration());
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actualOpt = tx->getOfferSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOfferSequence should be present";
        expectEqualField(expected, *actualOpt, "sfOfferSequence");
        EXPECT_TRUE(tx->hasOfferSequence());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx->getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx->hasDomainID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsOfferCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testOfferCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const takerPaysValue = canonical_AMOUNT();
    auto const takerGetsValue = canonical_AMOUNT();
    auto const expirationValue = canonical_UINT32();
    auto const offerSequenceValue = canonical_UINT32();
    auto const domainIDValue = canonical_UINT256();

    // Build an initial transaction
    OfferCreateBuilder initialBuilder{
        accountValue,
        takerPaysValue,
        takerGetsValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setExpiration(expirationValue);
    initialBuilder.setOfferSequence(offerSequenceValue);
    initialBuilder.setDomainID(domainIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    OfferCreateBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = takerPaysValue;
        auto const actual = rebuiltTx->getTakerPays();
        expectEqualField(expected, actual, "sfTakerPays");
    }

    {
        auto const& expected = takerGetsValue;
        auto const actual = rebuiltTx->getTakerGets();
        expectEqualField(expected, actual, "sfTakerGets");
    }

    // Verify optional fields
    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx->getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actualOpt = rebuiltTx->getOfferSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOfferSequence should be present";
        expectEqualField(expected, *actualOpt, "sfOfferSequence");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx->getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

}

}
