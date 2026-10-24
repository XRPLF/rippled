#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

/**
 * Continuous accrual, XLS Vault Continuous Accrual sections 4.1 to 4.3. Accrual
 * needs a CashBasis Vault, so featureLendingProtocolV1_2 stays off.
 */
class LoanAccrual_test : public LoanTestBase
{
private:
    // Origination takes on the loan's interest as a budget and a rate, and
    // leaves AssetsTotal alone: nothing has been earned yet.
    void
    testAccrualLoanSetOrigination()
    {
        testcase("accrual: LoanSet origination");
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
            .accountingMethod = kVaultAccountingAccrual,
        };

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        auto const vaultBefore = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultBefore);
        if (!vaultBefore)
            return;
        Number const assetsTotalBefore = vaultBefore->at(sfAssetsTotal);
        BEAST_EXPECT(vaultBefore->at(sfUnearnedInterest) == Number{});
        BEAST_EXPECT(vaultBefore->at(sfAccrualRate) == Number{});

        auto const brokerBefore = env.le(broker.brokerKeylet());
        BEAST_EXPECT(brokerBefore);
        if (!brokerBefore)
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerBefore->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, xrpAsset(10'000).value()),
            kCounterparty(lender),
            kInterestRate(TenthBips32{percentageToTenthBips(12)}),
            kPaymentTotal(4),
            kPaymentInterval(600),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        auto const vaultAfter = env.le(broker.vaultKeylet());
        BEAST_EXPECT(loanSle && vaultAfter);
        if (!loanSle || !vaultAfter)
            return;

        Number const interestDue =
            Number{loanSle->at(sfTotalValueOutstanding)} - loanSle->at(sfPrincipalOutstanding);
        BEAST_EXPECT(interestDue > Number{});

        // Nothing earned yet, so AssetsTotal has not moved.
        BEAST_EXPECT(vaultAfter->at(sfAssetsTotal) == assetsTotalBefore);
        // The whole of the loan's interest is now the budget the clock may draw
        // down, and the vault carries the loan's rate.
        BEAST_EXPECT(vaultAfter->at(sfUnearnedInterest) == interestDue);
        BEAST_EXPECT(vaultAfter->at(sfAccrualRate) > Number{});
        BEAST_EXPECT(vaultAfter->at(sfLastAccrualTime) > 0u);
    }

    // The single most important case in the specification: an on-time payment
    // must not credit interest the clock has already recognized.
    void
    testAccrualLoanPayDoesNotDoubleCount()
    {
        testcase("accrual: on-time LoanPay does not double count");
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
            .accountingMethod = kVaultAccountingAccrual,
        };

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        auto const brokerBefore = env.le(broker.brokerKeylet());
        BEAST_EXPECT(brokerBefore);
        if (!brokerBefore)
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerBefore->at(sfLoanSequence)));

        std::uint32_t const paymentInterval = 600;
        env(set(borrower, broker.brokerID, xrpAsset(10'000).value()),
            kCounterparty(lender),
            kInterestRate(TenthBips32{percentageToTenthBips(12)}),
            kPaymentTotal(4),
            kPaymentInterval(paymentInterval),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        if (!loanSle)
            return;

        // Stand just inside the first period, so nearly all of it has been
        // recognized by the clock and the payment is still on time.
        env.close(NetClock::time_point{NetClock::duration{loanSle->at(sfNextPaymentDueDate) - 30}});

        auto const vaultAtDue = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultAtDue);
        if (!vaultAtDue)
            return;
        Number const assetsTotalAtDue = vaultAtDue->at(sfAssetsTotal);
        Number const unearnedAtDue = vaultAtDue->at(sfUnearnedInterest);

        auto const loanAtDue = env.le(loanKeylet);
        BEAST_EXPECT(loanAtDue);
        if (!loanAtDue)
            return;
        Number const principalBefore = loanAtDue->at(sfPrincipalOutstanding);

        // The stored periodic payment is unrounded; a payment must cover it, so
        // round up to the loan's scale.
        STAmount const paymentAmount{
            broker.asset.raw(),
            roundToAsset(
                broker.asset.raw(),
                Number{loanSle->at(sfPeriodicPayment)},
                loanSle->at(sfLoanScale),
                Number::RoundingMode::Upward)};
        env(pay(borrower, loanKeylet.key, paymentAmount), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfterPay = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultAfterPay);
        if (!vaultAfterPay)
            return;

        Number const assetsGained = Number{vaultAfterPay->at(sfAssetsTotal)} - assetsTotalAtDue;

        // The interest this payment actually carried: with no management fee,
        // whatever did not reduce the principal.
        auto const loanAfterPay = env.le(loanKeylet);
        BEAST_EXPECT(loanAfterPay);
        if (!loanAfterPay)
            return;
        Number const principalPaid =
            principalBefore - Number{loanAfterPay->at(sfPrincipalOutstanding)};
        Number const interestPaid = Number{paymentAmount} - principalPaid;
        BEAST_EXPECT(interestPaid > Number{});

        // Settling at the payment credits what the clock earned over the period,
        // and the payment itself adds only the remainder the clock had not yet
        // reached. Were the received interest credited again on top, the vault
        // would gain about twice the interest paid: that is the double count
        // this accounting exists to avoid.
        BEAST_EXPECT(assetsGained > Number{});
        BEAST_EXPECT(assetsGained < interestPaid + (interestPaid / Number{2}));

        // The budget shrinks: the period just paid is no longer owed.
        BEAST_EXPECT(vaultAfterPay->at(sfUnearnedInterest) < unearnedAtDue);
        // Never negative, whatever the rounding.
        BEAST_EXPECT(vaultAfterPay->at(sfUnearnedInterest) >= Number{});
        BEAST_EXPECT(vaultAfterPay->at(sfAccrualRate) >= Number{});
    }

    // Impairing a loan stops it accruing; clearing the impairment takes its rate back on.
    void
    testAccrualImpairHaltsAccrual()
    {
        testcase("accrual: impair halts accrual");
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
            .accountingMethod = kVaultAccountingAccrual,
        };

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        auto const brokerBefore = env.le(broker.brokerKeylet());
        BEAST_EXPECT(brokerBefore);
        if (!brokerBefore)
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerBefore->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, xrpAsset(10'000).value()),
            kCounterparty(lender),
            kInterestRate(TenthBips32{percentageToTenthBips(12)}),
            kPaymentTotal(4),
            kPaymentInterval(600),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        auto const vaultLent = env.le(broker.vaultKeylet());
        BEAST_EXPECT(loanSle && vaultLent);
        if (!loanSle || !vaultLent)
            return;
        Number const rateWhileLending = vaultLent->at(sfAccrualRate);
        BEAST_EXPECT(rateWhileLending > Number{});

        // A loan can only be impaired once it is late.
        env.close(NetClock::time_point{NetClock::duration{loanSle->at(sfNextPaymentDueDate) + 60}});

        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();

        auto const vaultImpaired = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultImpaired);
        if (!vaultImpaired)
            return;
        // The vault carries only this loan, so its rate falls to nothing.
        BEAST_EXPECT(vaultImpaired->at(sfAccrualRate) < rateWhileLending);
        BEAST_EXPECT(vaultImpaired->at(sfAccrualRate) == Number{});

        // An impaired loan earns nothing, so the budget does not move while it
        // stays impaired.
        Number const unearnedImpaired = vaultImpaired->at(sfUnearnedInterest);
        Number const assetsImpaired = vaultImpaired->at(sfAssetsTotal);
        env.close(
            NetClock::time_point{NetClock::duration{loanSle->at(sfNextPaymentDueDate) + 100'000}});
        {
            auto const vaultIdle = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultIdle);
            if (!vaultIdle)
                return;
            BEAST_EXPECT(vaultIdle->at(sfUnearnedInterest) == unearnedImpaired);
            BEAST_EXPECT(vaultIdle->at(sfAssetsTotal) == assetsImpaired);
        }

        env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tesSUCCESS));
        env.close();

        auto const vaultRestored = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultRestored);
        if (!vaultRestored)
            return;
        BEAST_EXPECT(vaultRestored->at(sfAccrualRate) == rateWhileLending);
    }

    // Default takes the loan's rate off the vault and its unrecognized interest
    // out of the budget, whether or not the loan was impaired first, so the
    // vault recognizes nothing more for a loan that is gone.
    void
    testAccrualDefaultStopsAccrual(bool impairFirst)
    {
        testcase(
            std::string("accrual: default stops accrual") +
            (impairFirst ? " (impaired)" : " (never impaired)"));
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
            .accountingMethod = kVaultAccountingAccrual,
        };

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        auto const brokerBefore = env.le(broker.brokerKeylet());
        BEAST_EXPECT(brokerBefore);
        if (!brokerBefore)
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerBefore->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, xrpAsset(10'000).value()),
            kCounterparty(lender),
            kInterestRate(TenthBips32{percentageToTenthBips(12)}),
            kPaymentTotal(4),
            kPaymentInterval(600),
            kGracePeriod(60),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        if (!loanSle)
            return;
        std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);

        if (impairFirst)
        {
            env.close(NetClock::time_point{NetClock::duration{dueDate + 30}});
            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
            env.close();
        }

        // Past the grace period, so the loan can be defaulted.
        env.close(NetClock::time_point{NetClock::duration{dueDate + 120}});
        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();

        auto const vaultDefaulted = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultDefaulted);
        if (!vaultDefaulted)
            return;
        // The vault carried only this loan, so nothing is left accruing and no
        // interest is left to recognize.
        BEAST_EXPECT(vaultDefaulted->at(sfAccrualRate) == Number{});
        BEAST_EXPECT(vaultDefaulted->at(sfUnearnedInterest) == Number{});
        BEAST_EXPECT(vaultDefaulted->at(sfLossUnrealized) == Number{});

        // Time passing recognizes nothing more.
        env.close(NetClock::time_point{NetClock::duration{dueDate + 100'000}});
        auto const vaultLater = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultLater);
        if (!vaultLater)
            return;
        BEAST_EXPECT(vaultAccruedInterest(*env.current(), vaultLater) == Number{});
        BEAST_EXPECT(vaultLater->at(sfAssetsTotal) == vaultDefaulted->at(sfAssetsTotal));
    }

    // With two loans on the vault, impairing or defaulting one leaves the other's
    // rate and budget in place, and defaulting both clears the vault.
    void
    testAccrualTwoLoans()
    {
        testcase("accrual: two loans, one at a time");
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
            .accountingMethod = kVaultAccountingAccrual,
        };

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};
        TenthBips32 const interestRate{percentageToTenthBips(12)};

        auto const originate = [&](std::int64_t principal) -> Keylet {
            auto const brokerSle = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerSle);
            if (!brokerSle)
                return keylet::loan(broker.brokerID, SeqProxy::rawSequence(0));
            auto const loanKeylet =
                keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
            env(set(borrower, broker.brokerID, xrpAsset(principal).value()),
                kCounterparty(lender),
                kInterestRate(interestRate),
                kPaymentTotal(4),
                kPaymentInterval(600),
                kGracePeriod(60),
                Sig(sfCounterpartySignature, lender),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            return loanKeylet;
        };
        // Both loans in one ledger, so they share a due date.
        Keylet const first = originate(10'000);
        Keylet const second = originate(20'000);
        env.close();

        auto const firstSle = env.le(first);
        auto const secondSle = env.le(second);
        BEAST_EXPECT(firstSle && secondSle);
        if (!firstSle || !secondSle)
            return;
        std::uint32_t const dueDate = firstSle->at(sfNextPaymentDueDate);
        Number const firstRate =
            loanAccrualRate(firstSle->at(sfPrincipalOutstanding), interestRate);
        Number const secondRate =
            loanAccrualRate(secondSle->at(sfPrincipalOutstanding), interestRate);

        auto const vault = [&] { return env.le(broker.vaultKeylet()); };
        auto const lending = vault();
        BEAST_EXPECT(lending);
        if (!lending)
            return;
        BEAST_EXPECT(secondRate > Number{});
        // What impairing the first leaves, computed as LoanManage computes it.
        Number const rateLeft = Number{lending->at(sfAccrualRate)} - firstRate;

        // Impairing the first leaves the second's rate accruing.
        env.close(NetClock::time_point{NetClock::duration{dueDate + 30}});
        env(manage(lender, first.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();
        if (auto const v = vault(); BEAST_EXPECT(v))
            BEAST_EXPECT(v->at(sfAccrualRate) == rateLeft);

        // Defaulting the first leaves the second's budget for it to earn.
        env.close(NetClock::time_point{NetClock::duration{dueDate + 120}});
        env(manage(lender, first.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();
        if (auto const v = vault(); BEAST_EXPECT(v))
        {
            BEAST_EXPECT(v->at(sfAccrualRate) == rateLeft);
            BEAST_EXPECT(v->at(sfUnearnedInterest) > Number{});
        }

        // Defaulting the second, never impaired, clears the vault.
        env(manage(lender, second.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();
        if (auto const v = vault(); BEAST_EXPECT(v))
        {
            BEAST_EXPECT(v->at(sfAccrualRate) == Number{});
            BEAST_EXPECT(v->at(sfUnearnedInterest) == Number{});
        }
    }

    // Without the amendment a lending vault keeps cash basis and none of the
    // loan transactions touch accrual state.
    void
    testAccrualOffWithoutAmendment()
    {
        testcase("accrual: off without the amendment");
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
        };

        Env env{
            *this,
            testableAmendments() - featureLendingProtocolV1_2 - featureVaultContinuousAccrual};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        auto const brokerSle = env.le(broker.brokerKeylet());
        BEAST_EXPECT(brokerSle);
        if (!brokerSle)
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, xrpAsset(10'000).value()),
            kCounterparty(lender),
            kInterestRate(TenthBips32{percentageToTenthBips(12)}),
            kPaymentTotal(4),
            kPaymentInterval(600),
            kGracePeriod(60),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        if (!loanSle)
            return;

        STAmount const paymentAmount{
            broker.asset.raw(),
            roundToAsset(
                broker.asset.raw(),
                Number{loanSle->at(sfPeriodicPayment)},
                loanSle->at(sfLoanScale),
                Number::RoundingMode::Upward)};
        env(pay(borrower, loanKeylet.key, paymentAmount), Ter(tesSUCCESS));
        env.close();

        auto const paidSle = env.le(loanKeylet);
        BEAST_EXPECT(paidSle);
        if (!paidSle)
            return;
        std::uint32_t const dueDate = paidSle->at(sfNextPaymentDueDate);
        env.close(NetClock::time_point{NetClock::duration{dueDate + 30}});
        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();
        env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tesSUCCESS));
        env.close();
        env.close(NetClock::time_point{NetClock::duration{dueDate + 120}});
        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();

        auto const vaultSle = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultSle);
        if (!vaultSle)
            return;
        BEAST_EXPECT(!vaultSle->isFieldPresent(sfAccountingMethod));
        BEAST_EXPECT(!vaultSle->isFieldPresent(sfAccrualRate));
        BEAST_EXPECT(!vaultSle->isFieldPresent(sfUnearnedInterest));
        BEAST_EXPECT(!vaultSle->isFieldPresent(sfLastAccrualTime));
    }

    // A read in the ledger a loan settled has no elapsed time to add.
    void
    testAccrualSameLedgerPricing()
    {
        testcase("accrual: pricing in the settling ledger");
        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0},
            .accountingMethod = kVaultAccountingAccrual,
        };

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        env(set(borrower, broker.brokerID, xrpAsset(10'000).value()),
            kCounterparty(lender),
            kInterestRate(TenthBips32{percentageToTenthBips(12)}),
            kPaymentTotal(4),
            kPaymentInterval(600),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));

        // Same open ledger as the LoanSet that stamped sfLastAccrualTime.
        auto const vaultSle = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultSle);
        if (!vaultSle)
            return;
        BEAST_EXPECT(vaultSle->at(sfAccrualRate) > Number{});
        BEAST_EXPECT(vaultAccruedInterest(*env.current(), vaultSle) == Number{});
        env.close();

        // A ledger later the same read has time to add.
        env.close();
        auto const vaultLater = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultLater);
        if (!vaultLater)
            return;
        BEAST_EXPECT(vaultAccruedInterest(*env.current(), vaultLater) > Number{});
    }

public:
    void
    run() override
    {
        testAccrualLoanSetOrigination();
        testAccrualLoanPayDoesNotDoubleCount();
        testAccrualImpairHaltsAccrual();
        testAccrualDefaultStopsAccrual(false);
        testAccrualDefaultStopsAccrual(true);
        testAccrualTwoLoans();
        testAccrualOffWithoutAmendment();
        testAccrualSameLedgerPricing();
    }
};

BEAST_DEFINE_TESTSUITE(LoanAccrual, tx, xrpl);

}  // namespace xrpl::test
