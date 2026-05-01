// Auto-generated unit tests for transaction XChainAddAccountCreateAttestation


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/XChainAddAccountCreateAttestation.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsXChainAddAccountCreateAttestationTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAddAccountCreateAttestation"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const attestationSignerAccountValue = canonical_ACCOUNT();
    auto const publicKeyValue = canonical_VL();
    auto const signatureValue = canonical_VL();
    auto const otherChainSourceValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const attestationRewardAccountValue = canonical_ACCOUNT();
    auto const wasLockingChainSendValue = canonical_UINT8();
    auto const xChainAccountCreateCountValue = canonical_UINT64();
    auto const destinationValue = canonical_ACCOUNT();
    auto const signatureRewardValue = canonical_AMOUNT();

    XChainAddAccountCreateAttestationBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setXChainBridge(xChainBridgeValue);
    builder.setAttestationSignerAccount(attestationSignerAccountValue);
    builder.setPublicKey(publicKeyValue);
    builder.setSignature(signatureValue);
    builder.setOtherChainSource(otherChainSourceValue);
    builder.setAmount(amountValue);
    builder.setAttestationRewardAccount(attestationRewardAccountValue);
    builder.setWasLockingChainSend(wasLockingChainSendValue);
    builder.setXChainAccountCreateCount(xChainAccountCreateCountValue);
    builder.setDestination(destinationValue);
    builder.setSignatureReward(signatureRewardValue);

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
        auto const& expected = xChainBridgeValue;
        auto const actualOpt = tx.getXChainBridge();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfXChainBridge should be present";
        expectEqualField(expected, *actualOpt, "sfXChainBridge");
        EXPECT_TRUE(tx.hasXChainBridge());
    }

    {
        auto const& expected = attestationSignerAccountValue;
        auto const actualOpt = tx.getAttestationSignerAccount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAttestationSignerAccount should be present";
        expectEqualField(expected, *actualOpt, "sfAttestationSignerAccount");
        EXPECT_TRUE(tx.hasAttestationSignerAccount());
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = tx.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfPublicKey");
        EXPECT_TRUE(tx.hasPublicKey());
    }

    {
        auto const& expected = signatureValue;
        auto const actualOpt = tx.getSignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignature should be present";
        expectEqualField(expected, *actualOpt, "sfSignature");
        EXPECT_TRUE(tx.hasSignature());
    }

    {
        auto const& expected = otherChainSourceValue;
        auto const actualOpt = tx.getOtherChainSource();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOtherChainSource should be present";
        expectEqualField(expected, *actualOpt, "sfOtherChainSource");
        EXPECT_TRUE(tx.hasOtherChainSource());
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = tx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
        EXPECT_TRUE(tx.hasAmount());
    }

    {
        auto const& expected = attestationRewardAccountValue;
        auto const actualOpt = tx.getAttestationRewardAccount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAttestationRewardAccount should be present";
        expectEqualField(expected, *actualOpt, "sfAttestationRewardAccount");
        EXPECT_TRUE(tx.hasAttestationRewardAccount());
    }

    {
        auto const& expected = wasLockingChainSendValue;
        auto const actualOpt = tx.getWasLockingChainSend();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWasLockingChainSend should be present";
        expectEqualField(expected, *actualOpt, "sfWasLockingChainSend");
        EXPECT_TRUE(tx.hasWasLockingChainSend());
    }

    {
        auto const& expected = xChainAccountCreateCountValue;
        auto const actualOpt = tx.getXChainAccountCreateCount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfXChainAccountCreateCount should be present";
        expectEqualField(expected, *actualOpt, "sfXChainAccountCreateCount");
        EXPECT_TRUE(tx.hasXChainAccountCreateCount());
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx.hasDestination());
    }

    {
        auto const& expected = signatureRewardValue;
        auto const actualOpt = tx.getSignatureReward();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignatureReward should be present";
        expectEqualField(expected, *actualOpt, "sfSignatureReward");
        EXPECT_TRUE(tx.hasSignatureReward());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsXChainAddAccountCreateAttestationTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAddAccountCreateAttestationFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const attestationSignerAccountValue = canonical_ACCOUNT();
    auto const publicKeyValue = canonical_VL();
    auto const signatureValue = canonical_VL();
    auto const otherChainSourceValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const attestationRewardAccountValue = canonical_ACCOUNT();
    auto const wasLockingChainSendValue = canonical_UINT8();
    auto const xChainAccountCreateCountValue = canonical_UINT64();
    auto const destinationValue = canonical_ACCOUNT();
    auto const signatureRewardValue = canonical_AMOUNT();

    // Build an initial transaction
    XChainAddAccountCreateAttestationBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setXChainBridge(xChainBridgeValue);
    initialBuilder.setAttestationSignerAccount(attestationSignerAccountValue);
    initialBuilder.setPublicKey(publicKeyValue);
    initialBuilder.setSignature(signatureValue);
    initialBuilder.setOtherChainSource(otherChainSourceValue);
    initialBuilder.setAmount(amountValue);
    initialBuilder.setAttestationRewardAccount(attestationRewardAccountValue);
    initialBuilder.setWasLockingChainSend(wasLockingChainSendValue);
    initialBuilder.setXChainAccountCreateCount(xChainAccountCreateCountValue);
    initialBuilder.setDestination(destinationValue);
    initialBuilder.setSignatureReward(signatureRewardValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    XChainAddAccountCreateAttestationBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = xChainBridgeValue;
        auto const actualOpt = rebuiltTx.getXChainBridge();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfXChainBridge should be present";
        expectEqualField(expected, *actualOpt, "sfXChainBridge");
    }

    {
        auto const& expected = attestationSignerAccountValue;
        auto const actualOpt = rebuiltTx.getAttestationSignerAccount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAttestationSignerAccount should be present";
        expectEqualField(expected, *actualOpt, "sfAttestationSignerAccount");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actualOpt = rebuiltTx.getPublicKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPublicKey should be present";
        expectEqualField(expected, *actualOpt, "sfPublicKey");
    }

    {
        auto const& expected = signatureValue;
        auto const actualOpt = rebuiltTx.getSignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignature should be present";
        expectEqualField(expected, *actualOpt, "sfSignature");
    }

    {
        auto const& expected = otherChainSourceValue;
        auto const actualOpt = rebuiltTx.getOtherChainSource();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOtherChainSource should be present";
        expectEqualField(expected, *actualOpt, "sfOtherChainSource");
    }

    {
        auto const& expected = amountValue;
        auto const actualOpt = rebuiltTx.getAmount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAmount should be present";
        expectEqualField(expected, *actualOpt, "sfAmount");
    }

    {
        auto const& expected = attestationRewardAccountValue;
        auto const actualOpt = rebuiltTx.getAttestationRewardAccount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfAttestationRewardAccount should be present";
        expectEqualField(expected, *actualOpt, "sfAttestationRewardAccount");
    }

    {
        auto const& expected = wasLockingChainSendValue;
        auto const actualOpt = rebuiltTx.getWasLockingChainSend();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWasLockingChainSend should be present";
        expectEqualField(expected, *actualOpt, "sfWasLockingChainSend");
    }

    {
        auto const& expected = xChainAccountCreateCountValue;
        auto const actualOpt = rebuiltTx.getXChainAccountCreateCount();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfXChainAccountCreateCount should be present";
        expectEqualField(expected, *actualOpt, "sfXChainAccountCreateCount");
    }

    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = signatureRewardValue;
        auto const actualOpt = rebuiltTx.getSignatureReward();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSignatureReward should be present";
        expectEqualField(expected, *actualOpt, "sfSignatureReward");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsXChainAddAccountCreateAttestationTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainAddAccountCreateAttestation{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsXChainAddAccountCreateAttestationTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainAddAccountCreateAttestationBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsXChainAddAccountCreateAttestationTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAddAccountCreateAttestationNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    XChainAddAccountCreateAttestationBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasXChainBridge());
    EXPECT_FALSE(tx.getXChainBridge().has_value());
    EXPECT_FALSE(tx.hasAttestationSignerAccount());
    EXPECT_FALSE(tx.getAttestationSignerAccount().has_value());
    EXPECT_FALSE(tx.hasPublicKey());
    EXPECT_FALSE(tx.getPublicKey().has_value());
    EXPECT_FALSE(tx.hasSignature());
    EXPECT_FALSE(tx.getSignature().has_value());
    EXPECT_FALSE(tx.hasOtherChainSource());
    EXPECT_FALSE(tx.getOtherChainSource().has_value());
    EXPECT_FALSE(tx.hasAmount());
    EXPECT_FALSE(tx.getAmount().has_value());
    EXPECT_FALSE(tx.hasAttestationRewardAccount());
    EXPECT_FALSE(tx.getAttestationRewardAccount().has_value());
    EXPECT_FALSE(tx.hasWasLockingChainSend());
    EXPECT_FALSE(tx.getWasLockingChainSend().has_value());
    EXPECT_FALSE(tx.hasXChainAccountCreateCount());
    EXPECT_FALSE(tx.getXChainAccountCreateCount().has_value());
    EXPECT_FALSE(tx.hasDestination());
    EXPECT_FALSE(tx.getDestination().has_value());
    EXPECT_FALSE(tx.hasSignatureReward());
    EXPECT_FALSE(tx.getSignatureReward().has_value());
}

}
