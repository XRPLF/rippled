// Auto-generated unit tests for transaction AccountSet


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>
#include <xrpl/protocol_autogen/transactions/OfferCancel.h>

#include <string>

namespace xrpl::transactions {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsAccountSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAccountSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const emailHashValue = canonical_UINT128();
    auto const walletLocatorValue = canonical_UINT256();
    auto const walletSizeValue = canonical_UINT32();
    auto const messageKeyValue = canonical_VL();
    auto const domainValue = canonical_VL();
    auto const transferRateValue = canonical_UINT32();
    auto const setFlagValue = canonical_UINT32();
    auto const clearFlagValue = canonical_UINT32();
    auto const tickSizeValue = canonical_UINT8();
    auto const nFTokenMinterValue = canonical_ACCOUNT();

    AccountSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setEmailHash(emailHashValue);
    builder.setWalletLocator(walletLocatorValue);
    builder.setWalletSize(walletSizeValue);
    builder.setMessageKey(messageKeyValue);
    builder.setDomain(domainValue);
    builder.setTransferRate(transferRateValue);
    builder.setSetFlag(setFlagValue);
    builder.setClearFlag(clearFlagValue);
    builder.setTickSize(tickSizeValue);
    builder.setNFTokenMinter(nFTokenMinterValue);

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
        auto const& expected = emailHashValue;
        auto const actualOpt = tx.getEmailHash();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfEmailHash should be present";
        expectEqualField(expected, *actualOpt, "sfEmailHash");
        EXPECT_TRUE(tx.hasEmailHash());
    }

    {
        auto const& expected = walletLocatorValue;
        auto const actualOpt = tx.getWalletLocator();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWalletLocator should be present";
        expectEqualField(expected, *actualOpt, "sfWalletLocator");
        EXPECT_TRUE(tx.hasWalletLocator());
    }

    {
        auto const& expected = walletSizeValue;
        auto const actualOpt = tx.getWalletSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWalletSize should be present";
        expectEqualField(expected, *actualOpt, "sfWalletSize");
        EXPECT_TRUE(tx.hasWalletSize());
    }

    {
        auto const& expected = messageKeyValue;
        auto const actualOpt = tx.getMessageKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMessageKey should be present";
        expectEqualField(expected, *actualOpt, "sfMessageKey");
        EXPECT_TRUE(tx.hasMessageKey());
    }

    {
        auto const& expected = domainValue;
        auto const actualOpt = tx.getDomain();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomain should be present";
        expectEqualField(expected, *actualOpt, "sfDomain");
        EXPECT_TRUE(tx.hasDomain());
    }

    {
        auto const& expected = transferRateValue;
        auto const actualOpt = tx.getTransferRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferRate should be present";
        expectEqualField(expected, *actualOpt, "sfTransferRate");
        EXPECT_TRUE(tx.hasTransferRate());
    }

    {
        auto const& expected = setFlagValue;
        auto const actualOpt = tx.getSetFlag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSetFlag should be present";
        expectEqualField(expected, *actualOpt, "sfSetFlag");
        EXPECT_TRUE(tx.hasSetFlag());
    }

    {
        auto const& expected = clearFlagValue;
        auto const actualOpt = tx.getClearFlag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfClearFlag should be present";
        expectEqualField(expected, *actualOpt, "sfClearFlag");
        EXPECT_TRUE(tx.hasClearFlag());
    }

    {
        auto const& expected = tickSizeValue;
        auto const actualOpt = tx.getTickSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTickSize should be present";
        expectEqualField(expected, *actualOpt, "sfTickSize");
        EXPECT_TRUE(tx.hasTickSize());
    }

    {
        auto const& expected = nFTokenMinterValue;
        auto const actualOpt = tx.getNFTokenMinter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenMinter should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenMinter");
        EXPECT_TRUE(tx.hasNFTokenMinter());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsAccountSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAccountSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const emailHashValue = canonical_UINT128();
    auto const walletLocatorValue = canonical_UINT256();
    auto const walletSizeValue = canonical_UINT32();
    auto const messageKeyValue = canonical_VL();
    auto const domainValue = canonical_VL();
    auto const transferRateValue = canonical_UINT32();
    auto const setFlagValue = canonical_UINT32();
    auto const clearFlagValue = canonical_UINT32();
    auto const tickSizeValue = canonical_UINT8();
    auto const nFTokenMinterValue = canonical_ACCOUNT();

    // Build an initial transaction
    AccountSetBuilder initialBuilder{
        accountValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setEmailHash(emailHashValue);
    initialBuilder.setWalletLocator(walletLocatorValue);
    initialBuilder.setWalletSize(walletSizeValue);
    initialBuilder.setMessageKey(messageKeyValue);
    initialBuilder.setDomain(domainValue);
    initialBuilder.setTransferRate(transferRateValue);
    initialBuilder.setSetFlag(setFlagValue);
    initialBuilder.setClearFlag(clearFlagValue);
    initialBuilder.setTickSize(tickSizeValue);
    initialBuilder.setNFTokenMinter(nFTokenMinterValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    AccountSetBuilder builderFromTx{initialTx.getSTTx()};

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
        auto const& expected = emailHashValue;
        auto const actualOpt = rebuiltTx.getEmailHash();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfEmailHash should be present";
        expectEqualField(expected, *actualOpt, "sfEmailHash");
    }

    {
        auto const& expected = walletLocatorValue;
        auto const actualOpt = rebuiltTx.getWalletLocator();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWalletLocator should be present";
        expectEqualField(expected, *actualOpt, "sfWalletLocator");
    }

    {
        auto const& expected = walletSizeValue;
        auto const actualOpt = rebuiltTx.getWalletSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfWalletSize should be present";
        expectEqualField(expected, *actualOpt, "sfWalletSize");
    }

    {
        auto const& expected = messageKeyValue;
        auto const actualOpt = rebuiltTx.getMessageKey();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfMessageKey should be present";
        expectEqualField(expected, *actualOpt, "sfMessageKey");
    }

    {
        auto const& expected = domainValue;
        auto const actualOpt = rebuiltTx.getDomain();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfDomain should be present";
        expectEqualField(expected, *actualOpt, "sfDomain");
    }

    {
        auto const& expected = transferRateValue;
        auto const actualOpt = rebuiltTx.getTransferRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTransferRate should be present";
        expectEqualField(expected, *actualOpt, "sfTransferRate");
    }

    {
        auto const& expected = setFlagValue;
        auto const actualOpt = rebuiltTx.getSetFlag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfSetFlag should be present";
        expectEqualField(expected, *actualOpt, "sfSetFlag");
    }

    {
        auto const& expected = clearFlagValue;
        auto const actualOpt = rebuiltTx.getClearFlag();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfClearFlag should be present";
        expectEqualField(expected, *actualOpt, "sfClearFlag");
    }

    {
        auto const& expected = tickSizeValue;
        auto const actualOpt = rebuiltTx.getTickSize();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfTickSize should be present";
        expectEqualField(expected, *actualOpt, "sfTickSize");
    }

    {
        auto const& expected = nFTokenMinterValue;
        auto const actualOpt = rebuiltTx.getNFTokenMinter();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfNFTokenMinter should be present";
        expectEqualField(expected, *actualOpt, "sfNFTokenMinter");
    }

}

// 3) Verify wrapper throws when constructed from wrong transaction type.
TEST(TransactionsAccountSetTests, WrapperThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongType"));
    auto const account = calcAccountID(pk);

    OfferCancelBuilder wrongBuilder{account, canonical_UINT32(), 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AccountSet{wrongTx.getSTTx()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong transaction type.
TEST(TransactionsAccountSetTests, BuilderThrowsOnWrongTxType)
{
    // Build a valid transaction of a different type
    auto const [pk, sk] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testWrongTypeBuilder"));
    auto const account = calcAccountID(pk);

    OfferCancelBuilder wrongBuilder{account, canonical_UINT32(), 1, canonical_AMOUNT()};
    auto wrongTx = wrongBuilder.build(pk, sk);

    EXPECT_THROW(AccountSetBuilder{wrongTx.getSTTx()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(TransactionsAccountSetTests, OptionalFieldsReturnNullopt)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testAccountSetNullopt"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 3;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific required field values

    AccountSetBuilder builder{
        accountValue,
        sequenceValue,
        feeValue
    };

    // Do NOT set optional fields

    auto tx = builder.build(publicKey, secretKey);

    // Verify optional fields are not present
    EXPECT_FALSE(tx.hasEmailHash());
    EXPECT_FALSE(tx.getEmailHash().has_value());
    EXPECT_FALSE(tx.hasWalletLocator());
    EXPECT_FALSE(tx.getWalletLocator().has_value());
    EXPECT_FALSE(tx.hasWalletSize());
    EXPECT_FALSE(tx.getWalletSize().has_value());
    EXPECT_FALSE(tx.hasMessageKey());
    EXPECT_FALSE(tx.getMessageKey().has_value());
    EXPECT_FALSE(tx.hasDomain());
    EXPECT_FALSE(tx.getDomain().has_value());
    EXPECT_FALSE(tx.hasTransferRate());
    EXPECT_FALSE(tx.getTransferRate().has_value());
    EXPECT_FALSE(tx.hasSetFlag());
    EXPECT_FALSE(tx.getSetFlag().has_value());
    EXPECT_FALSE(tx.hasClearFlag());
    EXPECT_FALSE(tx.getClearFlag().has_value());
    EXPECT_FALSE(tx.hasTickSize());
    EXPECT_FALSE(tx.getTickSize().has_value());
    EXPECT_FALSE(tx.hasNFTokenMinter());
    EXPECT_FALSE(tx.getNFTokenMinter().has_value());
}

}
