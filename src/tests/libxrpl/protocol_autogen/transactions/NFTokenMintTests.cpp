// Auto-generated unit tests for transaction NFTokenMint

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/NFTokenMint.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsNFTokenMintTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenMint"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const nFTokenTaxonValue = canonical_UINT32();
    auto const transferFeeValue = canonical_UINT16();
    auto const issuerValue = canonical_ACCOUNT();
    auto const uRIValue = canonical_VL();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const expirationValue = canonical_UINT32();

    NFTokenMintBuilder builder{
        accountValue,
        nFTokenTaxonValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setTransferFee(transferFeeValue);
    builder.setIssuer(issuerValue);
    builder.setURI(uRIValue);
    builder.setAmount(amountValue);
    builder.setDestination(destinationValue);
    builder.setExpiration(expirationValue);

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
        auto const& expected = nFTokenTaxonValue;
        auto const actual = tx->getNFTokenTaxon();
        expectEqualField(expected, actual, "sfNFTokenTaxon");
    }

    // Verify optional fields
    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = tx->getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
        EXPECT_TRUE(tx->hasTransferFee());
    }

    {
        auto const& expected = issuerValue;
        auto const actualOpt = tx->getIssuer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIssuer should be present";
        expectEqualField(expected, *actualOpt, "sfIssuer");
        EXPECT_TRUE(tx->hasIssuer());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = tx->getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(tx->hasURI());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = tx->getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(tx->hasAmount());
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx->getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx->hasDestination());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = tx->getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(tx->hasExpiration());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsNFTokenMintTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testNFTokenMintFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const nFTokenTaxonValue = canonical_UINT32();
    auto const transferFeeValue = canonical_UINT16();
    auto const issuerValue = canonical_ACCOUNT();
    auto const uRIValue = canonical_VL();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const expirationValue = canonical_UINT32();

    // Build an initial transaction
    NFTokenMintBuilder initialBuilder{
        accountValue,
        nFTokenTaxonValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setTransferFee(transferFeeValue);
    initialBuilder.setIssuer(issuerValue);
    initialBuilder.setURI(uRIValue);
    initialBuilder.setAmount(amountValue);
    initialBuilder.setDestination(destinationValue);
    initialBuilder.setExpiration(expirationValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    NFTokenMintBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = nFTokenTaxonValue;
        auto const actual = rebuiltTx->getNFTokenTaxon();
        expectEqualField(expected, actual, "sfNFTokenTaxon");
    }

    // Verify optional fields
    {
        auto const& expected = transferFeeValue;
        auto const actualOpt = rebuiltTx->getTransferFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferFee should be present";
        expectEqualField(expected, *actualOpt, "sfTransferFee");
    }

    {
        auto const& expected = issuerValue;
        auto const actualOpt = rebuiltTx->getIssuer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIssuer should be present";
        expectEqualField(expected, *actualOpt, "sfIssuer");
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = rebuiltTx->getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx->getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx->getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = rebuiltTx->getExpiration();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfExpiration should be present";
        expectEqualField(expected, *actualOpt, "sfExpiration");
    }

}

}
