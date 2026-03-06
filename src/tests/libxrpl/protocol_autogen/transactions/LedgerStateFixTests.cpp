// Auto-generated unit tests for transaction LedgerStateFix

#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LedgerStateFix.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLedgerStateFixTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLedgerStateFix"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ledgerFixTypeValue = canonical_UINT16();
    auto const ownerValue = canonical_ACCOUNT();

    LedgerStateFixBuilder builder{
        accountValue,
        ledgerFixTypeValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setOwner(ownerValue);

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
    {
        auto const& expected = ledgerFixTypeValue;
        auto const actual = tx->getLedgerFixType();
        expectEqualField(expected, actual, "sfLedgerFixType");
    }

    // Verify optional fields
    {
        auto const& expected = ownerValue;
        auto const actualOpt = tx->getOwner();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwner should be present";
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(tx->hasOwner());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsLedgerStateFixTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLedgerStateFixFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const ledgerFixTypeValue = canonical_UINT16();
    auto const ownerValue = canonical_ACCOUNT();

    // Build an initial transaction
    LedgerStateFixBuilder initialBuilder{
        accountValue,
        ledgerFixTypeValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setOwner(ownerValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LedgerStateFixBuilder builderFromTx{initialTx.object()};

    std::string reason;
    EXPECT_TRUE(builderFromTx.validate(reason)) << reason;

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);
    EXPECT_TRUE(rebuiltTx->validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx->getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx->getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx->getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = ledgerFixTypeValue;
        auto const actual = rebuiltTx->getLedgerFixType();
        expectEqualField(expected, actual, "sfLedgerFixType");
    }

    // Verify optional fields
    {
        auto const& expected = ownerValue;
        auto const actualOpt = rebuiltTx->getOwner();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOwner should be present";
        expectEqualField(expected, *actualOpt, "sfOwner");
    }

}

}
