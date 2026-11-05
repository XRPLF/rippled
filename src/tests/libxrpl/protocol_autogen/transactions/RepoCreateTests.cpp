// Auto-generated unit tests for transaction RepoCreate


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/RepoCreate.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsRepoCreateTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testRepoCreate"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const collateralAmountValue = canonical_AMOUNT();
    auto const purchasePriceValue = canonical_AMOUNT();
    auto const interestRateValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const maturityDateValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const dataValue = canonical_VL();

    RepoCreateBuilder builder{
        accountValue,
        counterpartyValue,
        collateralAmountValue,
        purchasePriceValue,
        interestRateValue,
        expirationValue,
        maturityDateValue,
        gracePeriodValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setData(dataValue);

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
        auto const& expected = counterpartyValue;
        auto const actual = tx.getCounterparty();
        expectEqualField(expected, actual, "sfCounterparty");
    }

    {
        auto const& expected = collateralAmountValue;
        auto const actual = tx.getCollateralAmount();
        expectEqualField(expected, actual, "sfCollateralAmount");
    }

    {
        auto const& expected = purchasePriceValue;
        auto const actual = tx.getPurchasePrice();
        expectEqualField(expected, actual, "sfPurchasePrice");
    }

    {
        auto const& expected = interestRateValue;
        auto const actual = tx.getInterestRate();
        expectEqualField(expected, actual, "sfInterestRate");
    }

    {
        auto const& expected = expirationValue;
        auto const actual = tx.getExpiration();
        expectEqualField(expected, actual, "sfExpiration");
    }

    {
        auto const& expected = maturityDateValue;
        auto const actual = tx.getMaturityDate();
        expectEqualField(expected, actual, "sfMaturityDate");
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actual = tx.getGracePeriod();
        expectEqualField(expected, actual, "sfGracePeriod");
    }

    // Verify optional fields
    {
        auto const& expected = dataValue;
        auto const actualOpt = tx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx.hasData());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsRepoCreateTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testRepoCreateFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const collateralAmountValue = canonical_AMOUNT();
    auto const purchasePriceValue = canonical_AMOUNT();
    auto const interestRateValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const maturityDateValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const dataValue = canonical_VL();

    // Build an initial transaction
    RepoCreateBuilder initialBuilder{
        accountValue,
        counterpartyValue,
        collateralAmountValue,
        purchasePriceValue,
        interestRateValue,
        expirationValue,
        maturityDateValue,
        gracePeriodValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setData(dataValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    RepoCreateBuilder builderFromTx{initialTx.getSTTx()};

    auto rebuiltTx = builderFromTx.build(publicKey, secretKey);

    std::string reason;
    EXPECT_TRUE(rebuiltTx.validate(reason)) << reason;

    // Verify common fields
    EXPECT_EQ(rebuiltTx.getAccount(), accountValue);
    EXPECT_EQ(rebuiltTx.getSequence(), sequenceValue);
    EXPECT_EQ(rebuiltTx.getFee(), feeValue);

    // Verify required fields
    {
        auto const& expected = counterpartyValue;
        auto const actual = rebuiltTx.getCounterparty();
        expectEqualField(expected, actual, "sfCounterparty");
    }

    {
        auto const& expected = collateralAmountValue;
        auto const actual = rebuiltTx.getCollateralAmount();
        expectEqualField(expected, actual, "sfCollateralAmount");
    }

    {
        auto const& expected = purchasePriceValue;
        auto const actual = rebuiltTx.getPurchasePrice();
        expectEqualField(expected, actual, "sfPurchasePrice");
    }

    {
        auto const& expected = interestRateValue;
        auto const actual = rebuiltTx.getInterestRate();
        expectEqualField(expected, actual, "sfInterestRate");
    }

    {
        auto const& expected = expirationValue;
        auto const actual = rebuiltTx.getExpiration();
        expectEqualField(expected, actual, "sfExpiration");
    }

    {
        auto const& expected = maturityDateValue;
        auto const actual = rebuiltTx.getMaturityDate();
        expectEqualField(expected, actual, "sfMaturityDate");
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actual = rebuiltTx.getGracePeriod();
        expectEqualField(expected, actual, "sfGracePeriod");
    }

    // Verify optional fields
    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx.getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsRepoCreateTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(RepoCreate{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsRepoCreateTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    AccountSetBuilder wrongBuilder{account, 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(RepoCreateBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsRepoCreateTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::Secp256k1, generateSeed("testRepoCreateNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const collateralAmountValue = canonical_AMOUNT();
    auto const purchasePriceValue = canonical_AMOUNT();
    auto const interestRateValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const maturityDateValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();

    RepoCreateBuilder builder{
        accountValue,
        counterpartyValue,
        collateralAmountValue,
        purchasePriceValue,
        interestRateValue,
        expirationValue,
        maturityDateValue,
        gracePeriodValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasData());
    EXPECT_FALSE(tx.getData().has_value());
}

}
