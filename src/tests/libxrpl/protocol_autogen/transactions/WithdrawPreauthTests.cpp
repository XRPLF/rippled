// Auto-generated unit tests for transaction WithdrawPreauth


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/WithdrawPreauth.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsWithdrawPreauthTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWithdrawPreauth"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const authorizeValue = canonical_ACCOUNT();
    auto const unauthorizeValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    WithdrawPreauthBuilder builder{
        accountValue,
        counterpartySignatureValue,
        firewallIDValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setAuthorize(authorizeValue);
    builder.setUnauthorize(unauthorizeValue);
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
        auto const& expected = counterpartySignatureValue;
        auto const actual = tx.getCounterpartySignature();
        expectEqualField(expected, actual, "sfCounterpartySignature");
    }

    {
        auto const& expected = firewallIDValue;
        auto const actual = tx.getFirewallID();
        expectEqualField(expected, actual, "sfFirewallID");
    }

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
        auto const& expected = destinationTagValue;
        auto const actualOpt = tx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
        EXPECT_TRUE(tx.hasDestinationTag());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsWithdrawPreauthTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWithdrawPreauthFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const authorizeValue = canonical_ACCOUNT();
    auto const unauthorizeValue = canonical_ACCOUNT();
    auto const destinationTagValue = canonical_UINT32();
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    // Build an initial transaction
    WithdrawPreauthBuilder initialBuilder{
        accountValue,
        counterpartySignatureValue,
        firewallIDValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setAuthorize(authorizeValue);
    initialBuilder.setUnauthorize(unauthorizeValue);
    initialBuilder.setDestinationTag(destinationTagValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    WithdrawPreauthBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = counterpartySignatureValue;
        auto const actual = rebuiltTx.getCounterpartySignature();
        expectEqualField(expected, actual, "sfCounterpartySignature");
    }

    {
        auto const& expected = firewallIDValue;
        auto const actual = rebuiltTx.getFirewallID();
        expectEqualField(expected, actual, "sfFirewallID");
    }

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
        auto const& expected = destinationTagValue;
        auto const actualOpt = rebuiltTx.getDestinationTag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDestinationTag should be present";
        expectEqualField(expected, *actualOpt, "sfDestinationTag");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsWithdrawPreauthTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(WithdrawPreauth{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsWithdrawPreauthTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(WithdrawPreauthBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsWithdrawPreauthTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWithdrawPreauthNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const firewallIDValue = canonical_UINT256();

    WithdrawPreauthBuilder builder{
        accountValue,
        counterpartySignatureValue,
        firewallIDValue,
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
    EXPECT_FALSE(tx.hasDestinationTag());
    EXPECT_FALSE(tx.getDestinationTag().has_value());
}

}
