// Auto-generated unit tests for transaction SignerListSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/SignerListSet.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsSignerListSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSignerListSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const signerQuorumValue = canonical_UINT32();
    auto const signerEntriesValue = canonical_ARRAY();

    SignerListSetBuilder builder{
        accountValue,
        signerQuorumValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setSignerEntries(signerEntriesValue);

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
        auto const& expected = signerQuorumValue;
        auto const actual = tx.getSignerQuorum();
        expectEqualField(expected, actual, "sfSignerQuorum");
    }

    // Verify optional fields
    {
        auto const& expected = signerEntriesValue;
        auto const actualOpt = tx.getSignerEntries();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignerEntries should be present";
        expectEqualField(expected, *actualOpt, "sfSignerEntries");
        EXPECT_TRUE(tx.hasSignerEntries());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsSignerListSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSignerListSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const signerQuorumValue = canonical_UINT32();
    auto const signerEntriesValue = canonical_ARRAY();

    // Build an initial transaction
    SignerListSetBuilder initialBuilder{
        accountValue,
        signerQuorumValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setSignerEntries(signerEntriesValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    SignerListSetBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = signerQuorumValue;
        auto const actual = rebuiltTx.getSignerQuorum();
        expectEqualField(expected, actual, "sfSignerQuorum");
    }

    // Verify optional fields
    {
        auto const& expected = signerEntriesValue;
        auto const actualOpt = rebuiltTx.getSignerEntries();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignerEntries should be present";
        expectEqualField(expected, *actualOpt, "sfSignerEntries");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsSignerListSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SignerListSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsSignerListSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(SignerListSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsSignerListSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testSignerListSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const signerQuorumValue = canonical_UINT32();

    SignerListSetBuilder builder{
        accountValue,
        signerQuorumValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasSignerEntries());
    EXPECT_FALSE(tx.getSignerEntries().has_value());
}

}
