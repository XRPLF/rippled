// Auto-generated unit tests for transaction LoanSet

#include <gtest/gtest.h>

#include <protocol_autogen/CanonicalValues.h>

#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol_autogen/transactions/LoanSet.h>

#include <string>

namespace xrpl::transactions {



// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed.
TEST(TransactionsLoanSetTests, BuilderSettersRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanSet"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 1;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const dataValue = canonical_VL();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const loanOriginationFeeValue = canonical_NUMBER();
    auto const loanServiceFeeValue = canonical_NUMBER();
    auto const latePaymentFeeValue = canonical_NUMBER();
    auto const closePaymentFeeValue = canonical_NUMBER();
    auto const overpaymentFeeValue = canonical_UINT32();
    auto const interestRateValue = canonical_UINT32();
    auto const lateInterestRateValue = canonical_UINT32();
    auto const closeInterestRateValue = canonical_UINT32();
    auto const overpaymentInterestRateValue = canonical_UINT32();
    auto const principalRequestedValue = canonical_NUMBER();
    auto const paymentTotalValue = canonical_UINT32();
    auto const paymentIntervalValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();

    LoanSetBuilder builder{
        accountValue,
        loanBrokerIDValue,
        principalRequestedValue,
        sequenceValue,
        feeValue
    };

    // Set optional fields
    builder.setData(dataValue);
    builder.setCounterparty(counterpartyValue);
    builder.setCounterpartySignature(counterpartySignatureValue);
    builder.setLoanOriginationFee(loanOriginationFeeValue);
    builder.setLoanServiceFee(loanServiceFeeValue);
    builder.setLatePaymentFee(latePaymentFeeValue);
    builder.setClosePaymentFee(closePaymentFeeValue);
    builder.setOverpaymentFee(overpaymentFeeValue);
    builder.setInterestRate(interestRateValue);
    builder.setLateInterestRate(lateInterestRateValue);
    builder.setCloseInterestRate(closeInterestRateValue);
    builder.setOverpaymentInterestRate(overpaymentInterestRateValue);
    builder.setPaymentTotal(paymentTotalValue);
    builder.setPaymentInterval(paymentIntervalValue);
    builder.setGracePeriod(gracePeriodValue);

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
        auto const& expected = loanBrokerIDValue;
        auto const actual = tx->getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    {
        auto const& expected = principalRequestedValue;
        auto const actual = tx->getPrincipalRequested();
        expectEqualField(expected, actual, "sfPrincipalRequested");
    }

    // Verify optional fields
    {
        auto const& expected = dataValue;
        auto const actualOpt = tx->getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(tx->hasData());
    }

    {
        auto const& expected = counterpartyValue;
        auto const actualOpt = tx->getCounterparty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterparty should be present";
        expectEqualField(expected, *actualOpt, "sfCounterparty");
        EXPECT_TRUE(tx->hasCounterparty());
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = tx->getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
        EXPECT_TRUE(tx->hasCounterpartySignature());
    }

    {
        auto const& expected = loanOriginationFeeValue;
        auto const actualOpt = tx->getLoanOriginationFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanOriginationFee should be present";
        expectEqualField(expected, *actualOpt, "sfLoanOriginationFee");
        EXPECT_TRUE(tx->hasLoanOriginationFee());
    }

    {
        auto const& expected = loanServiceFeeValue;
        auto const actualOpt = tx->getLoanServiceFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanServiceFee should be present";
        expectEqualField(expected, *actualOpt, "sfLoanServiceFee");
        EXPECT_TRUE(tx->hasLoanServiceFee());
    }

    {
        auto const& expected = latePaymentFeeValue;
        auto const actualOpt = tx->getLatePaymentFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLatePaymentFee should be present";
        expectEqualField(expected, *actualOpt, "sfLatePaymentFee");
        EXPECT_TRUE(tx->hasLatePaymentFee());
    }

    {
        auto const& expected = closePaymentFeeValue;
        auto const actualOpt = tx->getClosePaymentFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfClosePaymentFee should be present";
        expectEqualField(expected, *actualOpt, "sfClosePaymentFee");
        EXPECT_TRUE(tx->hasClosePaymentFee());
    }

    {
        auto const& expected = overpaymentFeeValue;
        auto const actualOpt = tx->getOverpaymentFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOverpaymentFee should be present";
        expectEqualField(expected, *actualOpt, "sfOverpaymentFee");
        EXPECT_TRUE(tx->hasOverpaymentFee());
    }

    {
        auto const& expected = interestRateValue;
        auto const actualOpt = tx->getInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfInterestRate");
        EXPECT_TRUE(tx->hasInterestRate());
    }

    {
        auto const& expected = lateInterestRateValue;
        auto const actualOpt = tx->getLateInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLateInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfLateInterestRate");
        EXPECT_TRUE(tx->hasLateInterestRate());
    }

    {
        auto const& expected = closeInterestRateValue;
        auto const actualOpt = tx->getCloseInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCloseInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfCloseInterestRate");
        EXPECT_TRUE(tx->hasCloseInterestRate());
    }

    {
        auto const& expected = overpaymentInterestRateValue;
        auto const actualOpt = tx->getOverpaymentInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOverpaymentInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfOverpaymentInterestRate");
        EXPECT_TRUE(tx->hasOverpaymentInterestRate());
    }

    {
        auto const& expected = paymentTotalValue;
        auto const actualOpt = tx->getPaymentTotal();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPaymentTotal should be present";
        expectEqualField(expected, *actualOpt, "sfPaymentTotal");
        EXPECT_TRUE(tx->hasPaymentTotal());
    }

    {
        auto const& expected = paymentIntervalValue;
        auto const actualOpt = tx->getPaymentInterval();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPaymentInterval should be present";
        expectEqualField(expected, *actualOpt, "sfPaymentInterval");
        EXPECT_TRUE(tx->hasPaymentInterval());
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actualOpt = tx->getGracePeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfGracePeriod should be present";
        expectEqualField(expected, *actualOpt, "sfGracePeriod");
        EXPECT_TRUE(tx->hasGracePeriod());
    }

}

// 2 & 4) Start from an STTx, construct a builder from it, build a new wrapper,
// and verify all fields match.
TEST(TransactionsLoanSetTests, BuilderFromStTxRoundTrip)
{
    // Generate a deterministic keypair for signing
    auto const [publicKey, secretKey] =
        generateKeyPair(KeyType::secp256k1, generateSeed("testLoanSetFromTx"));

    // Common transaction fields
    auto const accountValue = calcAccountID(publicKey);
    std::uint32_t const sequenceValue = 2;
    auto const feeValue = canonical_AMOUNT();

    // Transaction-specific field values
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const dataValue = canonical_VL();
    auto const counterpartyValue = canonical_ACCOUNT();
    auto const counterpartySignatureValue = canonical_OBJECT();
    auto const loanOriginationFeeValue = canonical_NUMBER();
    auto const loanServiceFeeValue = canonical_NUMBER();
    auto const latePaymentFeeValue = canonical_NUMBER();
    auto const closePaymentFeeValue = canonical_NUMBER();
    auto const overpaymentFeeValue = canonical_UINT32();
    auto const interestRateValue = canonical_UINT32();
    auto const lateInterestRateValue = canonical_UINT32();
    auto const closeInterestRateValue = canonical_UINT32();
    auto const overpaymentInterestRateValue = canonical_UINT32();
    auto const principalRequestedValue = canonical_NUMBER();
    auto const paymentTotalValue = canonical_UINT32();
    auto const paymentIntervalValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();

    // Build an initial transaction
    LoanSetBuilder initialBuilder{
        accountValue,
        loanBrokerIDValue,
        principalRequestedValue,
        sequenceValue,
        feeValue
    };

    initialBuilder.setData(dataValue);
    initialBuilder.setCounterparty(counterpartyValue);
    initialBuilder.setCounterpartySignature(counterpartySignatureValue);
    initialBuilder.setLoanOriginationFee(loanOriginationFeeValue);
    initialBuilder.setLoanServiceFee(loanServiceFeeValue);
    initialBuilder.setLatePaymentFee(latePaymentFeeValue);
    initialBuilder.setClosePaymentFee(closePaymentFeeValue);
    initialBuilder.setOverpaymentFee(overpaymentFeeValue);
    initialBuilder.setInterestRate(interestRateValue);
    initialBuilder.setLateInterestRate(lateInterestRateValue);
    initialBuilder.setCloseInterestRate(closeInterestRateValue);
    initialBuilder.setOverpaymentInterestRate(overpaymentInterestRateValue);
    initialBuilder.setPaymentTotal(paymentTotalValue);
    initialBuilder.setPaymentInterval(paymentIntervalValue);
    initialBuilder.setGracePeriod(gracePeriodValue);

    auto initialTx = initialBuilder.build(publicKey, secretKey);

    // Create builder from existing STTx
    LoanSetBuilder builderFromTx{initialTx.object()};

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
        auto const& expected = loanBrokerIDValue;
        auto const actual = rebuiltTx->getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    {
        auto const& expected = principalRequestedValue;
        auto const actual = rebuiltTx->getPrincipalRequested();
        expectEqualField(expected, actual, "sfPrincipalRequested");
    }

    // Verify optional fields
    {
        auto const& expected = dataValue;
        auto const actualOpt = rebuiltTx->getData();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfData should be present";
        expectEqualField(expected, *actualOpt, "sfData");
    }

    {
        auto const& expected = counterpartyValue;
        auto const actualOpt = rebuiltTx->getCounterparty();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterparty should be present";
        expectEqualField(expected, *actualOpt, "sfCounterparty");
    }

    {
        auto const& expected = counterpartySignatureValue;
        auto const actualOpt = rebuiltTx->getCounterpartySignature();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCounterpartySignature should be present";
        expectEqualField(expected, *actualOpt, "sfCounterpartySignature");
    }

    {
        auto const& expected = loanOriginationFeeValue;
        auto const actualOpt = rebuiltTx->getLoanOriginationFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanOriginationFee should be present";
        expectEqualField(expected, *actualOpt, "sfLoanOriginationFee");
    }

    {
        auto const& expected = loanServiceFeeValue;
        auto const actualOpt = rebuiltTx->getLoanServiceFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLoanServiceFee should be present";
        expectEqualField(expected, *actualOpt, "sfLoanServiceFee");
    }

    {
        auto const& expected = latePaymentFeeValue;
        auto const actualOpt = rebuiltTx->getLatePaymentFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLatePaymentFee should be present";
        expectEqualField(expected, *actualOpt, "sfLatePaymentFee");
    }

    {
        auto const& expected = closePaymentFeeValue;
        auto const actualOpt = rebuiltTx->getClosePaymentFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfClosePaymentFee should be present";
        expectEqualField(expected, *actualOpt, "sfClosePaymentFee");
    }

    {
        auto const& expected = overpaymentFeeValue;
        auto const actualOpt = rebuiltTx->getOverpaymentFee();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOverpaymentFee should be present";
        expectEqualField(expected, *actualOpt, "sfOverpaymentFee");
    }

    {
        auto const& expected = interestRateValue;
        auto const actualOpt = rebuiltTx->getInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfInterestRate");
    }

    {
        auto const& expected = lateInterestRateValue;
        auto const actualOpt = rebuiltTx->getLateInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfLateInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfLateInterestRate");
    }

    {
        auto const& expected = closeInterestRateValue;
        auto const actualOpt = rebuiltTx->getCloseInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfCloseInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfCloseInterestRate");
    }

    {
        auto const& expected = overpaymentInterestRateValue;
        auto const actualOpt = rebuiltTx->getOverpaymentInterestRate();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfOverpaymentInterestRate should be present";
        expectEqualField(expected, *actualOpt, "sfOverpaymentInterestRate");
    }

    {
        auto const& expected = paymentTotalValue;
        auto const actualOpt = rebuiltTx->getPaymentTotal();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPaymentTotal should be present";
        expectEqualField(expected, *actualOpt, "sfPaymentTotal");
    }

    {
        auto const& expected = paymentIntervalValue;
        auto const actualOpt = rebuiltTx->getPaymentInterval();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfPaymentInterval should be present";
        expectEqualField(expected, *actualOpt, "sfPaymentInterval");
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actualOpt = rebuiltTx->getGracePeriod();
        ASSERT_TRUE(actualOpt.has_value()) << "Optional field sfGracePeriod should be present";
        expectEqualField(expected, *actualOpt, "sfGracePeriod");
    }

}

}
