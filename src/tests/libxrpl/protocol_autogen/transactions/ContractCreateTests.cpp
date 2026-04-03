// Auto-generated unit tests for transaction ContractCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/ContractCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsContractCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testContractCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const contractCodeValue = canonical_VL();
    auto const contractHashValue = canonical_UINT256();
    auto const functionsValue = canonical_ARRAY();
    auto const instanceParametersValue = canonical_ARRAY();
    auto const instanceParameterValuesValue = canonical_ARRAY();
    auto const uRIValue = canonical_VL();

    ContractCreateBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setContractCode(contractCodeValue);
    builder.setContractHash(contractHashValue);
    builder.setFunctions(functionsValue);
    builder.setInstanceParameters(instanceParametersValue);
    builder.setInstanceParameterValues(instanceParameterValuesValue);
    builder.setURI(uRIValue);

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
        auto const& expected = contractCodeValue;
        auto const actualOpt = tx.getContractCode();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfContractCode should be present";
        expectEqualField(expected, *actualOpt, "sfContractCode");
        EXPECT_TRUE(tx.hasContractCode());
    }

    {
        auto const& expected = contractHashValue;
        auto const actualOpt = tx.getContractHash();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfContractHash should be present";
        expectEqualField(expected, *actualOpt, "sfContractHash");
        EXPECT_TRUE(tx.hasContractHash());
    }

    {
        auto const& expected = functionsValue;
        auto const actualOpt = tx.getFunctions();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFunctions should be present";
        expectEqualField(expected, *actualOpt, "sfFunctions");
        EXPECT_TRUE(tx.hasFunctions());
    }

    {
        auto const& expected = instanceParametersValue;
        auto const actualOpt = tx.getInstanceParameters();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInstanceParameters should be present";
        expectEqualField(expected, *actualOpt, "sfInstanceParameters");
        EXPECT_TRUE(tx.hasInstanceParameters());
    }

    {
        auto const& expected = instanceParameterValuesValue;
        auto const actualOpt = tx.getInstanceParameterValues();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInstanceParameterValues should be present";
        expectEqualField(expected, *actualOpt, "sfInstanceParameterValues");
        EXPECT_TRUE(tx.hasInstanceParameterValues());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = tx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(tx.hasURI());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsContractCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testContractCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const contractCodeValue = canonical_VL();
    auto const contractHashValue = canonical_UINT256();
    auto const functionsValue = canonical_ARRAY();
    auto const instanceParametersValue = canonical_ARRAY();
    auto const instanceParameterValuesValue = canonical_ARRAY();
    auto const uRIValue = canonical_VL();

    // Build an initial transaction
    ContractCreateBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setContractCode(contractCodeValue);
    initialBuilder.setContractHash(contractHashValue);
    initialBuilder.setFunctions(functionsValue);
    initialBuilder.setInstanceParameters(instanceParametersValue);
    initialBuilder.setInstanceParameterValues(instanceParameterValuesValue);
    initialBuilder.setURI(uRIValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ContractCreateBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = contractCodeValue;
        auto const actualOpt = rebuiltTx.getContractCode();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfContractCode should be present";
        expectEqualField(expected, *actualOpt, "sfContractCode");
    }

    {
        auto const& expected = contractHashValue;
        auto const actualOpt = rebuiltTx.getContractHash();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfContractHash should be present";
        expectEqualField(expected, *actualOpt, "sfContractHash");
    }

    {
        auto const& expected = functionsValue;
        auto const actualOpt = rebuiltTx.getFunctions();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfFunctions should be present";
        expectEqualField(expected, *actualOpt, "sfFunctions");
    }

    {
        auto const& expected = instanceParametersValue;
        auto const actualOpt = rebuiltTx.getInstanceParameters();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInstanceParameters should be present";
        expectEqualField(expected, *actualOpt, "sfInstanceParameters");
    }

    {
        auto const& expected = instanceParameterValuesValue;
        auto const actualOpt = rebuiltTx.getInstanceParameterValues();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInstanceParameterValues should be present";
        expectEqualField(expected, *actualOpt, "sfInstanceParameterValues");
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = rebuiltTx.getURI();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfURI should be present";
        expectEqualField(expected, *actualOpt, "sfURI");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsContractCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ContractCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsContractCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(ContractCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsContractCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testContractCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    ContractCreateBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasContractCode());
    EXPECT_FALSE(tx.getContractCode().has_value());
    EXPECT_FALSE(tx.hasContractHash());
    EXPECT_FALSE(tx.getContractHash().has_value());
    EXPECT_FALSE(tx.hasFunctions());
    EXPECT_FALSE(tx.getFunctions().has_value());
    EXPECT_FALSE(tx.hasInstanceParameters());
    EXPECT_FALSE(tx.getInstanceParameters().has_value());
    EXPECT_FALSE(tx.hasInstanceParameterValues());
    EXPECT_FALSE(tx.getInstanceParameterValues().has_value());
    EXPECT_FALSE(tx.hasURI());
    EXPECT_FALSE(tx.getURI().has_value());
}

}
