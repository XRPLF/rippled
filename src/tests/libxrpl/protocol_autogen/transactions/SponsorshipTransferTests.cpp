// Auto-generated unit tests for transaction SponsorshipTransfer


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/SponsorshipTransfer.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsSponsorshipTransferTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSponsorshipTransfer"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const objectIDValue = canonical_UINT256();
    auto const sponseeValue = canonical_ACCOUNT();

    SponsorshipTransferBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setObjectID(objectIDValue);
    builder.setSponsee(sponseeValue);

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
        auto const& expected = objectIDValue;
        auto const actualOpt = tx.getObjectID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfObjectID should be present";
        expectEqualField(expected, *actualOpt, "sfObjectID");
        EXPECT_TRUE(tx.hasObjectID());
    }

    {
        auto const& expected = sponseeValue;
        auto const actualOpt = tx.getSponsee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSponsee should be present";
        expectEqualField(expected, *actualOpt, "sfSponsee");
        EXPECT_TRUE(tx.hasSponsee());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsSponsorshipTransferTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSponsorshipTransferFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const objectIDValue = canonical_UINT256();
    auto const sponseeValue = canonical_ACCOUNT();

    // Build an initial transaction
    SponsorshipTransferBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setObjectID(objectIDValue);
    initialBuilder.setSponsee(sponseeValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    SponsorshipTransferBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = objectIDValue;
        auto const actualOpt = rebuiltTx.getObjectID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfObjectID should be present";
        expectEqualField(expected, *actualOpt, "sfObjectID");
    }

    {
        auto const& expected = sponseeValue;
        auto const actualOpt = rebuiltTx.getSponsee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSponsee should be present";
        expectEqualField(expected, *actualOpt, "sfSponsee");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsSponsorshipTransferTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SponsorshipTransfer{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsSponsorshipTransferTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SponsorshipTransferBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsSponsorshipTransferTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSponsorshipTransferNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    SponsorshipTransferBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasObjectID());
    EXPECT_FALSE(tx.getObjectID().has_value());
    EXPECT_FALSE(tx.hasSponsee());
    EXPECT_FALSE(tx.getSponsee().has_value());
}

}
