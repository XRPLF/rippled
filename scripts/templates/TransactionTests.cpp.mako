// Auto-generated unit tests for transaction ${name}

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/${name}.h>

#include <string>

namespace xrpl::transactions {

<%
    required_fields = [f for f in fields if f["requirement"] == "soeREQUIRED"]
    optional_fields = [f for f in fields if f["requirement"] != "soeREQUIRED"]

    def canonical_expr(field):
        return f"canonical_{field['stiSuffix']}()"
%>

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

    std::string reason;
    EXPECT_TRUE(builder.validate(reason)) << reason;

    auto tx = builder.build(publicKey, secretKey);

    EXPECT_TRUE(tx->validate(reason)) << reason;

    // Verify signing was applied
    EXPECT_FALSE(tx->getSigningPubKey().empty());
    EXPECT_TRUE(tx->hasTxnSignature());

    // Verify common fields
    EXPECT_EQ(tx->getAccount(), accountValue);
    EXPECT_EQ(tx->getSequence(), sequenceValue);
    EXPECT_EQ(tx->getFee(), feeValue);

    // Verify required fields
% for field in required_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actual = tx->get${field["name"][2:]}();
        expectEqualField(expected, actual, "${field["name"]}");
    }

% endfor
    // Verify optional fields
% for field in optional_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actualOpt = tx->get${field["name"][2:]}();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field ${field["name"]} should be present";
        expectEqualField(expected, *actualOpt, "${field["name"]}");
        EXPECT_TRUE(tx->has${field["name"][2:]}());
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
    ${name}Builder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
% for field in required_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actual = rebuiltTx->get${field["name"][2:]}();
        expectEqualField(expected, actual, "${field["name"]}");
    }

% endfor
    // Verify optional fields
% for field in optional_fields:
    {
        auto const& expected = ${field["paramName"]}Value;
        auto const actualOpt = rebuiltTx->get${field["name"][2:]}();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field ${field["name"]} should be present";
        expectEqualField(expected, *actualOpt, "${field["name"]}");
    }

% endfor
}

}
