// Auto-generated unit tests for transaction ${name}
<%
    required_fields = [f for f in fields if f["requirement"] == "soeREQUIRED"]
    optional_fields = [f for f in fields if f["requirement"] != "soeREQUIRED"]

    def canonical_expr(field):
        return f"canonical_{field['stiSuffix']}()"

    # Pick a wrong transaction to test type mismatch
    if name != "AccountSet":
        wrong_tx_include = "AccountSet"
    else:
        wrong_tx_include = "OfferCancel"
%>

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/${name}.h>
#include <xrpl/protocol_autogen/transactions/${wrong_tx_include}.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(Transactions${name}Tests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("test${name}"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
% for field in fields:
    auto const ${field["paramName"]}Value = ${canonical_expr(field)};
% endfor

    ${name}Builder builder{
        accountValue,
% for field in required_fields:
        ${field["paramName"]}Value,
% endfor
        sequenceValue,
        feeValue
    };

    // Set optional fields
% for field in optional_fields:
    builder.set${field["name"][2:]}(${field["paramName"]}Value);
% endfor

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
% for field in required_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actual = tx.get${field["name"][2:]}();
        expectEqualField(expected, actual, "${field["name"]}");
    }

% endfor
    // Verify optional fields
% for field in optional_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actualOpt = tx.get${field["name"][2:]}();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field ${field["name"]} should be present";
        expectEqualField(expected, *actualOpt, "${field["name"]}");
        EXPECT_TRUE(tx.has${field["name"][2:]}());
    }

% endfor
}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(Transactions${name}Tests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("test${name}FromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
% for field in fields:
    auto const ${field["paramName"]}Value = ${canonical_expr(field)};
% endfor

    // Build an initial transaction
    ${name}Builder initialBuilder{
        accountValue,
% for field in required_fields:
        ${field["paramName"]}Value,
% endfor
        sequenceValue,
        feeValue
    };

% for field in optional_fields:
    initialBuilder.set${field["name"][2:]}(${field["paramName"]}Value);
% endfor

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    ${name}Builder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
% for field in required_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actual = rebuiltTx.get${field["name"][2:]}();
        expectEqualField(expected, actual, "${field["name"]}");
    }

% endfor
    // Verify optional fields
% for field in optional_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actualOpt = rebuiltTx.get${field["name"][2:]}();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field ${field["name"]} should be present";
        expectEqualField(expected, *actualOpt, "${field["name"]}");
    }

% endfor
}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(Transactions${name}Tests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

% if wrong_tx_include == "AccountSet":
    ${wrong_tx_include}Builder wrongBuilder{account, 1, canonical_AMOUNT()};
% else:
    ${wrong_tx_include}Builder wrongBuilder{account, canonical_UINT32(), 1, canonical_AMOUNT()};
% endif
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(${name}{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(Transactions${name}Tests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

% if wrong_tx_include == "AccountSet":
    ${wrong_tx_include}Builder wrongBuilder{account, 1, canonical_AMOUNT()};
% else:
    ${wrong_tx_include}Builder wrongBuilder{account, canonical_UINT32(), 1, canonical_AMOUNT()};
% endif
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(${name}Builder{wrongTx.getSTTx()}, std::runtime_error);
}

% if optional_fields:
// 5) Build with only required fields and verify optional fields return nullopt.
TEST(Transactions${name}Tests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("test${name}Nullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
% for field in required_fields:
    auto const ${field["paramName"]}Value = ${canonical_expr(field)};
% endfor

    ${name}Builder builder{
        accountValue,
% for field in required_fields:
        ${field["paramName"]}Value,
% endfor
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
% for field in optional_fields:
    EXPECT_FALSE(tx.has${field["name"][2:]}());
    EXPECT_FALSE(tx.get${field["name"][2:]}().has_value());
% endfor
}
% endif

}
