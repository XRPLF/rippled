// Auto-generated unit tests for transaction CredentialDelete


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/CredentialDelete.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsCredentialDeleteTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCredentialDelete"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const subjectValue = canonical_ACCOUNT();
    auto const issuerValue = canonical_ACCOUNT();
    auto const credentialTypeValue = canonical_VL();

    CredentialDeleteBuilder builder{
        accountValue,
        credentialTypeValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setSubject(subjectValue);
    builder.setIssuer(issuerValue);

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
        auto const& expected = credentialTypeValue;
        auto const actual = tx.getCredentialType();
        expectEqualField(expected, actual, "sfCredentialType");
    }

    // Verify optional fields
    {
        auto const& expected = subjectValue;
        auto const actualOpt = tx.getSubject();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubject should be present";
        expectEqualField(expected, *actualOpt, "sfSubject");
        EXPECT_TRUE(tx.hasSubject());
    }

    {
        auto const& expected = issuerValue;
        auto const actualOpt = tx.getIssuer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIssuer should be present";
        expectEqualField(expected, *actualOpt, "sfIssuer");
        EXPECT_TRUE(tx.hasIssuer());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsCredentialDeleteTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCredentialDeleteFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const subjectValue = canonical_ACCOUNT();
    auto const issuerValue = canonical_ACCOUNT();
    auto const credentialTypeValue = canonical_VL();

    // Build an initial transaction
    CredentialDeleteBuilder initialBuilder{
        accountValue,
        credentialTypeValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setSubject(subjectValue);
    initialBuilder.setIssuer(issuerValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    CredentialDeleteBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = credentialTypeValue;
        auto const actual = rebuiltTx.getCredentialType();
        expectEqualField(expected, actual, "sfCredentialType");
    }

    // Verify optional fields
    {
        auto const& expected = subjectValue;
        auto const actualOpt = rebuiltTx.getSubject();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSubject should be present";
        expectEqualField(expected, *actualOpt, "sfSubject");
    }

    {
        auto const& expected = issuerValue;
        auto const actualOpt = rebuiltTx.getIssuer();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfIssuer should be present";
        expectEqualField(expected, *actualOpt, "sfIssuer");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsCredentialDeleteTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CredentialDelete{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsCredentialDeleteTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(CredentialDeleteBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsCredentialDeleteTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testCredentialDeleteNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const credentialTypeValue = canonical_VL();

    CredentialDeleteBuilder builder{
        accountValue,
        credentialTypeValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasSubject());
    EXPECT_FALSE(tx.getSubject().has_value());
    EXPECT_FALSE(tx.hasIssuer());
    EXPECT_FALSE(tx.getIssuer().has_value());
}

}
