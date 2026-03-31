// Auto-generated unit tests for transaction XChainAddClaimAttestation


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/XChainAddClaimAttestation.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsXChainAddClaimAttestationTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAddClaimAttestation"));

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
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const destinationValue = canonical_ACCOUNT();

    XChainAddClaimAttestationBuilder builder{
        accountValue,
        xChainBridgeValue,
        attestationSignerAccountValue,
        publicKeyValue,
        signatureValue,
        otherChainSourceValue,
        amountValue,
        attestationRewardAccountValue,
        wasLockingChainSendValue,
        xChainClaimIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestination(destinationValue);

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
        auto const& expected = xChainBridgeValue;
        auto const actual = tx.getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    {
        auto const& expected = attestationSignerAccountValue;
        auto const actual = tx.getAttestationSignerAccount();
        expectEqualField(expected, actual, "sfAttestationSignerAccount");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actual = tx.getPublicKey();
        expectEqualField(expected, actual, "sfPublicKey");
    }

    {
        auto const& expected = signatureValue;
        auto const actual = tx.getSignature();
        expectEqualField(expected, actual, "sfSignature");
    }

    {
        auto const& expected = otherChainSourceValue;
        auto const actual = tx.getOtherChainSource();
        expectEqualField(expected, actual, "sfOtherChainSource");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = attestationRewardAccountValue;
        auto const actual = tx.getAttestationRewardAccount();
        expectEqualField(expected, actual, "sfAttestationRewardAccount");
    }

    {
        auto const& expected = wasLockingChainSendValue;
        auto const actual = tx.getWasLockingChainSend();
        expectEqualField(expected, actual, "sfWasLockingChainSend");
    }

    {
        auto const& expected = xChainClaimIDValue;
        auto const actual = tx.getXChainClaimID();
        expectEqualField(expected, actual, "sfXChainClaimID");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx.hasDestination());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsXChainAddClaimAttestationTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAddClaimAttestationFromTx"));

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
    auto const xChainClaimIDValue = canonical_UINT64();
    auto const destinationValue = canonical_ACCOUNT();

    // Build an initial transaction
    XChainAddClaimAttestationBuilder initialBuilder{
        accountValue,
        xChainBridgeValue,
        attestationSignerAccountValue,
        publicKeyValue,
        signatureValue,
        otherChainSourceValue,
        amountValue,
        attestationRewardAccountValue,
        wasLockingChainSendValue,
        xChainClaimIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestination(destinationValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    XChainAddClaimAttestationBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = xChainBridgeValue;
        auto const actual = rebuiltTx.getXChainBridge();
        expectEqualField(expected, actual, "sfXChainBridge");
    }

    {
        auto const& expected = attestationSignerAccountValue;
        auto const actual = rebuiltTx.getAttestationSignerAccount();
        expectEqualField(expected, actual, "sfAttestationSignerAccount");
    }

    {
        auto const& expected = publicKeyValue;
        auto const actual = rebuiltTx.getPublicKey();
        expectEqualField(expected, actual, "sfPublicKey");
    }

    {
        auto const& expected = signatureValue;
        auto const actual = rebuiltTx.getSignature();
        expectEqualField(expected, actual, "sfSignature");
    }

    {
        auto const& expected = otherChainSourceValue;
        auto const actual = rebuiltTx.getOtherChainSource();
        expectEqualField(expected, actual, "sfOtherChainSource");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = attestationRewardAccountValue;
        auto const actual = rebuiltTx.getAttestationRewardAccount();
        expectEqualField(expected, actual, "sfAttestationRewardAccount");
    }

    {
        auto const& expected = wasLockingChainSendValue;
        auto const actual = rebuiltTx.getWasLockingChainSend();
        expectEqualField(expected, actual, "sfWasLockingChainSend");
    }

    {
        auto const& expected = xChainClaimIDValue;
        auto const actual = rebuiltTx.getXChainClaimID();
        expectEqualField(expected, actual, "sfXChainClaimID");
    }

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsXChainAddClaimAttestationTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainAddClaimAttestation{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsXChainAddClaimAttestationTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(XChainAddClaimAttestationBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsXChainAddClaimAttestationTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testXChainAddClaimAttestationNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const xChainBridgeValue = canonical_XCHAIN_BRIDGE();
    auto const attestationSignerAccountValue = canonical_ACCOUNT();
    auto const publicKeyValue = canonical_VL();
    auto const signatureValue = canonical_VL();
    auto const otherChainSourceValue = canonical_ACCOUNT();
    auto const amountValue = canonical_AMOUNT();
    auto const attestationRewardAccountValue = canonical_ACCOUNT();
    auto const wasLockingChainSendValue = canonical_UINT8();
    auto const xChainClaimIDValue = canonical_UINT64();

    XChainAddClaimAttestationBuilder builder{
        accountValue,
        xChainBridgeValue,
        attestationSignerAccountValue,
        publicKeyValue,
        signatureValue,
        otherChainSourceValue,
        amountValue,
        attestationRewardAccountValue,
        wasLockingChainSendValue,
        xChainClaimIDValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDestination());
    EXPECT_FALSE(tx.getDestination().has_value());
}

}
