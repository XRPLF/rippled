// Auto-generated unit tests for transaction ConfidentialMPTHolderKeyUpdate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ConfidentialMPTHolderKeyUpdate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsConfidentialMPTHolderKeyUpdateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTHolderKeyUpdate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderEncryptionKeyValue = canonical_VL();
    auto const confidentialBalanceSpendingValue = canonical_VL();
    auto const confidentialBalanceInboxValue = canonical_VL();
    auto const zKProofValue = canonical_VL();

    ConfidentialMPTHolderKeyUpdateBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        holderEncryptionKeyValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setConfidentialBalanceSpending(confidentialBalanceSpendingValue);
    builder.setConfidentialBalanceInbox(confidentialBalanceInboxValue);

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
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = tx.getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = holderEncryptionKeyValue;
        auto const actual = tx.getHolderEncryptionKey();
        expectEqualField(expected, actual, "sfHolderEncryptionKey");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = tx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
    {
        auto const& expected = confidentialBalanceSpendingValue;
        auto const actualOpt = tx.getConfidentialBalanceSpending();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfConfidentialBalanceSpending should be present";
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceSpending");
        EXPECT_TRUE(tx.hasConfidentialBalanceSpending());
    }

    {
        auto const& expected = confidentialBalanceInboxValue;
        auto const actualOpt = tx.getConfidentialBalanceInbox();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfConfidentialBalanceInbox should be present";
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceInbox");
        EXPECT_TRUE(tx.hasConfidentialBalanceInbox());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsConfidentialMPTHolderKeyUpdateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTHolderKeyUpdateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderEncryptionKeyValue = canonical_VL();
    auto const confidentialBalanceSpendingValue = canonical_VL();
    auto const confidentialBalanceInboxValue = canonical_VL();
    auto const zKProofValue = canonical_VL();

    // Build an initial transaction
    ConfidentialMPTHolderKeyUpdateBuilder initialBuilder{
        accountValue,
        mPTokenIssuanceIDValue,
        holderEncryptionKeyValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setConfidentialBalanceSpending(confidentialBalanceSpendingValue);
    initialBuilder.setConfidentialBalanceInbox(confidentialBalanceInboxValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ConfidentialMPTHolderKeyUpdateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = rebuiltTx.getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = holderEncryptionKeyValue;
        auto const actual = rebuiltTx.getHolderEncryptionKey();
        expectEqualField(expected, actual, "sfHolderEncryptionKey");
    }

    {
        auto const& expected = zKProofValue;
        auto const actual = rebuiltTx.getZKProof();
        expectEqualField(expected, actual, "sfZKProof");
    }

    // Verify optional fields
    {
        auto const& expected = confidentialBalanceSpendingValue;
        auto const actualOpt = rebuiltTx.getConfidentialBalanceSpending();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfConfidentialBalanceSpending should be present";
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceSpending");
    }

    {
        auto const& expected = confidentialBalanceInboxValue;
        auto const actualOpt = rebuiltTx.getConfidentialBalanceInbox();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfConfidentialBalanceInbox should be present";
        expectEqualField(expected, *actualOpt, "sfConfidentialBalanceInbox");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTHolderKeyUpdateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTHolderKeyUpdate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsConfidentialMPTHolderKeyUpdateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ConfidentialMPTHolderKeyUpdateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsConfidentialMPTHolderKeyUpdateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testConfidentialMPTHolderKeyUpdateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const holderEncryptionKeyValue = canonical_VL();
    auto const zKProofValue = canonical_VL();

    ConfidentialMPTHolderKeyUpdateBuilder builder{
        accountValue,
        mPTokenIssuanceIDValue,
        holderEncryptionKeyValue,
        zKProofValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasConfidentialBalanceSpending());
    EXPECT_FALSE(tx.getConfidentialBalanceSpending().has_value());
    EXPECT_FALSE(tx.hasConfidentialBalanceInbox());
    EXPECT_FALSE(tx.getConfidentialBalanceInbox().has_value());
}

}
