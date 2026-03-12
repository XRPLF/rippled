// Auto-generated unit tests for transaction OfferCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/OfferCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

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
        auto const& expected = takerPaysValue;
        auto const actual = tx.getTakerPays();
        expectEqualField(expected, actual, "sfTakerPays");
    }

    {
        auto const& expected = takerGetsValue;
        auto const actual = tx.getTakerGets();
        expectEqualField(expected, actual, "sfTakerGets");
    }

    // Verify optional fields
    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx.hasExpiration());
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actualOpt = tx.getOfferSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOfferSequence should be present";
        expectEqualField(expected, *actualOpt, "sfOfferSequence");
        EXPECT_TRUE(tx.hasOfferSequence());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
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
    OfferCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = takerPaysValue;
        auto const actual = rebuiltTx.getTakerPays();
        expectEqualField(expected, actual, "sfTakerPays");
    }

    {
        auto const& expected = takerGetsValue;
        auto const actual = rebuiltTx.getTakerGets();
        expectEqualField(expected, actual, "sfTakerGets");
    }

    // Verify optional fields
    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx.getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

    {
        auto const& expected = offerSequenceValue;
        auto const actualOpt = rebuiltTx.getOfferSequence();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOfferSequence should be present";
        expectEqualField(expected, *actualOpt, "sfOfferSequence");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsOfferCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(OfferCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsOfferCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(OfferCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsOfferCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testOfferCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const takerPaysValue = canonical_AMOUNT();
    auto const takerGetsValue = canonical_AMOUNT();

    OfferCreateBuilder builder{
        accountValue,
        takerPaysValue,
        takerGetsValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasExpiration());
    EXPECT_FALSE(tx.getExpiration().has_value());
    EXPECT_FALSE(tx.hasOfferSequence());
    EXPECT_FALSE(tx.getOfferSequence().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
}

}
