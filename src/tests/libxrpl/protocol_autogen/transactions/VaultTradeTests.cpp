// Auto-generated unit tests for transaction VaultTrade


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/VaultTrade.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsVaultTradeTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultTrade"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const deliverMinValue = canonical_AMOUNT();
    auto const domainIDValue = canonical_UINT256();

    VaultTradeBuilder builder{
        accountValue,
        vaultIDValue,
        amountValue,
        sendMaxValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDeliverMin(deliverMinValue);
    builder.setDomainID(domainIDValue);

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
        auto const& expected = vaultIDValue;
        auto const actual = tx.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = tx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = sendMaxValue;
        auto const actual = tx.getSendMax();
        expectEqualField(expected, actual, "sfSendMax");
    }

    // Verify optional fields
    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = tx.getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
        EXPECT_TRUE(tx.hasDeliverMin());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = tx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(tx.hasDomainID());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsVaultTradeTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultTradeFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const sendMaxValue = canonical_AMOUNT();
    auto const deliverMinValue = canonical_AMOUNT();
    auto const domainIDValue = canonical_UINT256();

    // Build an initial transaction
    VaultTradeBuilder initialBuilder{
        accountValue,
        vaultIDValue,
        amountValue,
        sendMaxValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDeliverMin(deliverMinValue);
    initialBuilder.setDomainID(domainIDValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    VaultTradeBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = vaultIDValue;
        auto const actual = rebuiltTx.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
    }

    {
        auto const& expected = amountValue;
        auto const actual = rebuiltTx.getAmount();
        expectEqualField(expected, actual, "sfAmount");
    }

    {
        auto const& expected = sendMaxValue;
        auto const actual = rebuiltTx.getSendMax();
        expectEqualField(expected, actual, "sfSendMax");
    }

    // Verify optional fields
    {
        auto const& expected = deliverMinValue;
        auto const actualOpt = rebuiltTx.getDeliverMin();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDeliverMin should be present";
        expectEqualField(expected, *actualOpt, "sfDeliverMin");
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = rebuiltTx.getDomainID();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomainID should be present";
        expectEqualField(expected, *actualOpt, "sfDomainID");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsVaultTradeTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultTrade{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsVaultTradeTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultTradeBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsVaultTradeTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testVaultTradeNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const vaultIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const sendMaxValue = canonical_AMOUNT();

    VaultTradeBuilder builder{
        accountValue,
        vaultIDValue,
        amountValue,
        sendMaxValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDeliverMin());
    EXPECT_FALSE(tx.getDeliverMin().has_value());
    EXPECT_FALSE(tx.hasDomainID());
    EXPECT_FALSE(tx.getDomainID().has_value());
}

}
