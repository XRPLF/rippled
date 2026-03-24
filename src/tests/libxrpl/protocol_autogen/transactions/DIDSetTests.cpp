// Auto-generated unit tests for transaction DIDSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/DIDSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsDIDSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testDIDSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const dIDDocumentValue = canonical_VL();
    auto const uRIValue = canonical_VL();
    auto const dataValue = canonical_VL();

    DIDSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDIDDocument(dIDDocumentValue);
    builder.setURI(uRIValue);
    builder.setData(dataValue);

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
        auto const& expected = dIDDocumentValue;
        auto const actualOpt = tx.getDIDDocument();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDIDDocument should be present";
        expectEqualField(expected, *actualOpt, "sfDIDDocument");
        EXPECT_TRUE(tx.hasDIDDocument());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = tx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(tx.hasURI());
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = tx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx.hasData());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsDIDSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testDIDSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const dIDDocumentValue = canonical_VL();
    auto const uRIValue = canonical_VL();
    auto const dataValue = canonical_VL();

    // Build an initial transaction
    DIDSetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDIDDocument(dIDDocumentValue);
    initialBuilder.setURI(uRIValue);
    initialBuilder.setData(dataValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    DIDSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = dIDDocumentValue;
        auto const actualOpt = rebuiltTx.getDIDDocument();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDIDDocument should be present";
        expectEqualField(expected, *actualOpt, "sfDIDDocument");
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = rebuiltTx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsDIDSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(DIDSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsDIDSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(DIDSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsDIDSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testDIDSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    DIDSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDIDDocument());
    EXPECT_FALSE(tx.getDIDDocument().has_value());
    EXPECT_FALSE(tx.hasURI());
    EXPECT_FALSE(tx.getURI().has_value());
    EXPECT_FALSE(tx.hasData());
    EXPECT_FALSE(tx.getData().has_value());
}

}
