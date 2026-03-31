// Auto-generated unit tests for transaction VaultWithdraw


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/VaultWithdraw.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsVaultWithdrawTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testVaultWithdraw"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();

    VaultWithdrawBuilder builder{
        accountValue,
        vaultIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setDestination(destinationValue);
    builder.setDestinationTag(destinationTagValue);

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

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = tx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
        EXPECT_TRUE(tx.hasDestination());
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsVaultWithdrawTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testVaultWithdrawFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const vaultIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();
    auto const destinationValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();

    // Build an initial transaction
    VaultWithdrawBuilder initialBuilder{
        accountValue,
        vaultIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setDestination(destinationValue);
    initialBuilder.setDestinationTag(destinationTagValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    VaultWithdrawBuilder builderFromTx{initialTx.getSTTx()};

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

    // Verify optional fields
    {
        auto const& expected = destinationValue;
        auto const actualOpt = rebuiltTx.getDestination();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestination should be present";
        expectEqualField(expected, *actualOpt, "sfDestination");
    }

    {
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsVaultWithdrawTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultWithdraw{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsVaultWithdrawTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(VaultWithdrawBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsVaultWithdrawTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testVaultWithdrawNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const vaultIDValue = canonical_UINT256();
    auto const amountValue = canonical_AMOUNT();

    VaultWithdrawBuilder builder{
        accountValue,
        vaultIDValue,
        amountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasDestination());
    EXPECT_FALSE(tx.getDestination().has_value());
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
}

}
