// Auto-generated unit tests for transaction DepositPreauth


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/DepositPreauth.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsDepositPreauthTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testDepositPreauth"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const authorizeValue = canonical_ACCOUNT();
    auto const unauthorizeValue = canonical_ACCOUNT();
    auto const authorizeCredentialsValue = canonical_ARRAY();
    auto const unauthorizeCredentialsValue = canonical_ARRAY();

    DepositPreauthBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAuthorize(authorizeValue);
    builder.setUnauthorize(unauthorizeValue);
    builder.setAuthorizeCredentials(authorizeCredentialsValue);
    builder.setUnauthorizeCredentials(unauthorizeCredentialsValue);

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
        auto const& expected = authorizeValue;
        auto const actualOpt = tx.getAuthorize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthorize should be present";
        expectEqualField(expected, *actualOpt, "sfAuthorize");
        EXPECT_TRUE(tx.hasAuthorize());
    }

    {
        auto const& expected = unauthorizeValue;
        auto const actualOpt = tx.getUnauthorize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUnauthorize should be present";
        expectEqualField(expected, *actualOpt, "sfUnauthorize");
        EXPECT_TRUE(tx.hasUnauthorize());
    }

    {
        auto const& expected = authorizeCredentialsValue;
        auto const actualOpt = tx.getAuthorizeCredentials();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthorizeCredentials should be present";
        expectEqualField(expected, *actualOpt, "sfAuthorizeCredentials");
        EXPECT_TRUE(tx.hasAuthorizeCredentials());
    }

    {
        auto const& expected = unauthorizeCredentialsValue;
        auto const actualOpt = tx.getUnauthorizeCredentials();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUnauthorizeCredentials should be present";
        expectEqualField(expected, *actualOpt, "sfUnauthorizeCredentials");
        EXPECT_TRUE(tx.hasUnauthorizeCredentials());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsDepositPreauthTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testDepositPreauthFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const authorizeValue = canonical_ACCOUNT();
    auto const unauthorizeValue = canonical_ACCOUNT();
    auto const authorizeCredentialsValue = canonical_ARRAY();
    auto const unauthorizeCredentialsValue = canonical_ARRAY();

    // Build an initial transaction
    DepositPreauthBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAuthorize(authorizeValue);
    initialBuilder.setUnauthorize(unauthorizeValue);
    initialBuilder.setAuthorizeCredentials(authorizeCredentialsValue);
    initialBuilder.setUnauthorizeCredentials(unauthorizeCredentialsValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    DepositPreauthBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = authorizeValue;
        auto const actualOpt = rebuiltTx.getAuthorize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthorize should be present";
        expectEqualField(expected, *actualOpt, "sfAuthorize");
    }

    {
        auto const& expected = unauthorizeValue;
        auto const actualOpt = rebuiltTx.getUnauthorize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUnauthorize should be present";
        expectEqualField(expected, *actualOpt, "sfUnauthorize");
    }

    {
        auto const& expected = authorizeCredentialsValue;
        auto const actualOpt = rebuiltTx.getAuthorizeCredentials();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAuthorizeCredentials should be present";
        expectEqualField(expected, *actualOpt, "sfAuthorizeCredentials");
    }

    {
        auto const& expected = unauthorizeCredentialsValue;
        auto const actualOpt = rebuiltTx.getUnauthorizeCredentials();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfUnauthorizeCredentials should be present";
        expectEqualField(expected, *actualOpt, "sfUnauthorizeCredentials");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsDepositPreauthTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(DepositPreauth{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsDepositPreauthTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(DepositPreauthBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsDepositPreauthTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testDepositPreauthNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    DepositPreauthBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasAuthorize());
    EXPECT_FALSE(tx.getAuthorize().has_value());
    EXPECT_FALSE(tx.hasUnauthorize());
    EXPECT_FALSE(tx.getUnauthorize().has_value());
    EXPECT_FALSE(tx.hasAuthorizeCredentials());
    EXPECT_FALSE(tx.getAuthorizeCredentials().has_value());
    EXPECT_FALSE(tx.hasUnauthorizeCredentials());
    EXPECT_FALSE(tx.getUnauthorizeCredentials().has_value());
}

}
