// Auto-generated unit tests for transaction ContractUserDelete


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ContractUserDelete.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsContractUserDeleteTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testContractUserDelete"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const contractAccountValue = canonical_ACCOUNT();
    auto const functionNameValue = canonical_VL();
    auto const parametersValue = canonical_ARRAY();
    auto const computationAllowanceValue = canonical_UINT32();

    ContractUserDeleteBuilder builder{
        accountValue,
        contractAccountValue,
        functionNameValue,
        computationAllowanceValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setParameters(parametersValue);

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
        auto const& expected = contractAccountValue;
        auto const actual = tx.getContractAccount();
        expectEqualField(expected, actual, "sfContractAccount");
    }

    {
        auto const& expected = functionNameValue;
        auto const actual = tx.getFunctionName();
        expectEqualField(expected, actual, "sfFunctionName");
    }

    {
        auto const& expected = computationAllowanceValue;
        auto const actual = tx.getComputationAllowance();
        expectEqualField(expected, actual, "sfComputationAllowance");
    }

    // Verify optional fields
    {
        auto const& expected = parametersValue;
        auto const actualOpt = tx.getParameters();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfParameters should be present";
        expectEqualField(expected, *actualOpt, "sfParameters");
        EXPECT_TRUE(tx.hasParameters());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsContractUserDeleteTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testContractUserDeleteFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const contractAccountValue = canonical_ACCOUNT();
    auto const functionNameValue = canonical_VL();
    auto const parametersValue = canonical_ARRAY();
    auto const computationAllowanceValue = canonical_UINT32();

    // Build an initial transaction
    ContractUserDeleteBuilder initialBuilder{
        accountValue,
        contractAccountValue,
        functionNameValue,
        computationAllowanceValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setParameters(parametersValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ContractUserDeleteBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = contractAccountValue;
        auto const actual = rebuiltTx.getContractAccount();
        expectEqualField(expected, actual, "sfContractAccount");
    }

    {
        auto const& expected = functionNameValue;
        auto const actual = rebuiltTx.getFunctionName();
        expectEqualField(expected, actual, "sfFunctionName");
    }

    {
        auto const& expected = computationAllowanceValue;
        auto const actual = rebuiltTx.getComputationAllowance();
        expectEqualField(expected, actual, "sfComputationAllowance");
    }

    // Verify optional fields
    {
        auto const& expected = parametersValue;
        auto const actualOpt = rebuiltTx.getParameters();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfParameters should be present";
        expectEqualField(expected, *actualOpt, "sfParameters");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsContractUserDeleteTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ContractUserDelete{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsContractUserDeleteTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ContractUserDeleteBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsContractUserDeleteTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testContractUserDeleteNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const contractAccountValue = canonical_ACCOUNT();
    auto const functionNameValue = canonical_VL();
    auto const computationAllowanceValue = canonical_UINT32();

    ContractUserDeleteBuilder builder{
        accountValue,
        contractAccountValue,
        functionNameValue,
        computationAllowanceValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasParameters());
    EXPECT_FALSE(tx.getParameters().has_value());
}

}
