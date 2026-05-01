// Auto-generated unit tests for transaction NFTokenAcceptOffer


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/NFTokenAcceptOffer.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsNFTokenAcceptOfferTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenAcceptOffer"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const nFTokenBuyOfferValue = canonical_UINT256();
    auto const nFTokenSellOfferValue = canonical_UINT256();
    auto const nFTokenBrokerFeeValue = canonical_AMOUNT();

    NFTokenAcceptOfferBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setNFTokenBuyOffer(nFTokenBuyOfferValue);
    builder.setNFTokenSellOffer(nFTokenSellOfferValue);
    builder.setNFTokenBrokerFee(nFTokenBrokerFeeValue);

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
    // Verify optional fields
    {
        auto const& expected = nFTokenBuyOfferValue;
        auto const actualOpt = tx.getNFTokenBuyOffer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenBuyOffer should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenBuyOffer");
        EXPECT_TRUE(tx.hasNFTokenBuyOffer());
    }

    {
        auto const& expected = nFTokenSellOfferValue;
        auto const actualOpt = tx.getNFTokenSellOffer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenSellOffer should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenSellOffer");
        EXPECT_TRUE(tx.hasNFTokenSellOffer());
    }

    {
        auto const& expected = nFTokenBrokerFeeValue;
        auto const actualOpt = tx.getNFTokenBrokerFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenBrokerFee should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenBrokerFee");
        EXPECT_TRUE(tx.hasNFTokenBrokerFee());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsNFTokenAcceptOfferTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenAcceptOfferFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const nFTokenBuyOfferValue = canonical_UINT256();
    auto const nFTokenSellOfferValue = canonical_UINT256();
    auto const nFTokenBrokerFeeValue = canonical_AMOUNT();

    // Build an initial transaction
    NFTokenAcceptOfferBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setNFTokenBuyOffer(nFTokenBuyOfferValue);
    initialBuilder.setNFTokenSellOffer(nFTokenSellOfferValue);
    initialBuilder.setNFTokenBrokerFee(nFTokenBrokerFeeValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    NFTokenAcceptOfferBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    // Verify optional fields
    {
        auto const& expected = nFTokenBuyOfferValue;
        auto const actualOpt = rebuiltTx.getNFTokenBuyOffer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenBuyOffer should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenBuyOffer");
    }

    {
        auto const& expected = nFTokenSellOfferValue;
        auto const actualOpt = rebuiltTx.getNFTokenSellOffer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenSellOffer should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenSellOffer");
    }

    {
        auto const& expected = nFTokenBrokerFeeValue;
        auto const actualOpt = rebuiltTx.getNFTokenBrokerFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenBrokerFee should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenBrokerFee");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsNFTokenAcceptOfferTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(NFTokenAcceptOffer{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsNFTokenAcceptOfferTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(NFTokenAcceptOfferBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsNFTokenAcceptOfferTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenAcceptOfferNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    NFTokenAcceptOfferBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasNFTokenBuyOffer());
    EXPECT_FALSE(tx.getNFTokenBuyOffer().has_value());
    EXPECT_FALSE(tx.hasNFTokenSellOffer());
    EXPECT_FALSE(tx.getNFTokenSellOffer().has_value());
    EXPECT_FALSE(tx.hasNFTokenBrokerFee());
    EXPECT_FALSE(tx.getNFTokenBrokerFee().has_value());
}

}
