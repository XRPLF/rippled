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
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <tuple>

namespace xrpl::test {

// LendingProtocolV1_1 ("cash-basis" accounting) dedicated coverage.
//
// Existing tests never enable featureLendingProtocolV1_1 (see `all_`
// above), so these are the only tests in this file that exercise the
// amendment. They are called once, directly, from
// runAmendmentIndependent() -- not looped through
// runAmendmentSensitive()/amendmentCombinations(), since doing so would
// require re-deriving whole-life-specific expected values for ~15
// unrelated regression tests.
class LoanCashBasis_test : public LoanTestBase
{
private:
    // 1. LoanSet origination: Vault.AssetsTotal/LoanBroker.DebtTotal deltas,
    // and the AssetsMaximum/DebtMaximum guards (which always check against
    // principal + interestDue, regardless of the amendment).
    void
    testCashBasisLoanSetOrigination()
    {
        testcase("cash-basis: LoanSet origination");

        using namespace jtx;
        using namespace loan;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};

        Number const principalRequest{10'000};
        TenthBips32 const interestRate{percentageToTenthBips(10)};
        std::uint32_t const paymentTotal = 2;
        std::uint32_t const paymentInterval = 86400;

        // Creates a broker/vault, submits a single LoanSet with a nonzero
        // interest rate, and returns the observed Vault.AssetsTotal /
        // LoanBroker.DebtTotal deltas plus the loan's own computed
        // interestDue and principalOutstanding.
        auto runOrigination = [&](FeatureBitset features) {
            Env env(*this, features);

            Account const lender{"lender"};
            Account const borrower{"borrower"};
            env.fund(XRP(1'000'000), lender, borrower);
            env.close();

            BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

            auto const vaultBefore = env.le(broker.vaultKeylet());
            auto const brokerBefore = env.le(broker.brokerKeylet());
            BEAST_EXPECT(vaultBefore && brokerBefore);
            Number const assetsTotalBefore = vaultBefore->at(sfAssetsTotal);
            Number const debtTotalBefore = brokerBefore->at(sfDebtTotal);

            auto const loanSequence = brokerBefore->at(sfLoanSequence);
            auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

            env(set(borrower, broker.brokerID, xrpAsset(principalRequest).value()),
                kCounterparty(lender),
                kInterestRate(interestRate),
                kPaymentTotal(paymentTotal),
                kPaymentInterval(paymentInterval),
                Sig(sfCounterpartySignature, lender),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            auto const loanSle = env.le(loanKeylet);
            BEAST_EXPECT(loanSle);
            Number const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
            Number const totalValueOutstanding = loanSle->at(sfTotalValueOutstanding);
            Number const interestDue = totalValueOutstanding - principalOutstanding;
            BEAST_EXPECT(interestDue > beast::kZero);
            BEAST_EXPECT(principalOutstanding == xrpAsset(principalRequest).value());

            auto const vaultAfter = env.le(broker.vaultKeylet());
            auto const brokerAfter = env.le(broker.brokerKeylet());
            BEAST_EXPECT(vaultAfter && brokerAfter);
            Number const assetsTotalDelta =
                Number(vaultAfter->at(sfAssetsTotal)) - assetsTotalBefore;
            Number const debtTotalDelta = Number(brokerAfter->at(sfDebtTotal)) - debtTotalBefore;

            return std::make_tuple(
                assetsTotalDelta, debtTotalDelta, interestDue, principalOutstanding);
        };

        Number interestDueCash{};
        Number principalOutstandingCash{};
        {
            auto const [assetsTotalDelta, debtTotalDelta, interestDue, principalOutstanding] =
                runOrigination(all_ | featureLendingProtocolV1_1);
            interestDueCash = interestDue;
            principalOutstandingCash = principalOutstanding;

            BEAST_EXPECTS(
                assetsTotalDelta == beast::kZero,
                "cash-basis origination must not change AssetsTotal; delta=" +
                    to_string(assetsTotalDelta));
            BEAST_EXPECTS(
                debtTotalDelta == principalOutstanding,
                "cash-basis origination must add principal-only to DebtTotal; delta=" +
                    to_string(debtTotalDelta) + " principal=" + to_string(principalOutstanding));
        }

        {
            auto const [assetsTotalDelta, debtTotalDelta, interestDue, principalOutstanding] =
                runOrigination(all_);

            BEAST_EXPECTS(
                assetsTotalDelta == interestDue,
                "whole-life origination must add interestDue to AssetsTotal; delta=" +
                    to_string(assetsTotalDelta) + " interestDue=" + to_string(interestDue));
            BEAST_EXPECTS(
                debtTotalDelta == principalOutstanding + interestDue,
                "whole-life origination must add principal+interest to DebtTotal; delta=" +
                    to_string(debtTotalDelta));
        }

        // AssetsMaximum guard checks interestDue headroom only under
        // whole-life accounting; DebtMaximum guard also varies by model.
        auto runVaultGuard = [&](FeatureBitset features, Number const& slack, TER expected) {
            Env env(*this, features);

            Account const lender{"lender"};
            Account const borrower{"borrower"};
            env.fund(XRP(1'000'000), lender, borrower);
            env.close();

            BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

            auto const vaultSle = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultSle);
            Number const assetsTotalBefore = vaultSle->at(sfAssetsTotal);

            Vault const vault{env};
            auto tx = vault.set({.owner = lender, .id = broker.vaultID});
            tx[sfAssetsMaximum] = assetsTotalBefore + slack;
            env(tx);
            env.close();

            env(set(borrower, broker.brokerID, xrpAsset(principalRequest).value()),
                kCounterparty(lender),
                kInterestRate(interestRate),
                kPaymentTotal(paymentTotal),
                kPaymentInterval(paymentInterval),
                Sig(sfCounterpartySignature, lender),
                Fee(env.current()->fees().base * 2),
                Ter(expected));
            env.close();
        };

        auto runBrokerGuard = [&](FeatureBitset features, Number const& debtMaximum, TER expected) {
            Env env(*this, features);

            Account const lender{"lender"};
            Account const borrower{"borrower"};
            env.fund(XRP(1'000'000), lender, borrower);
            env.close();

            BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

            env(loan_broker::set(lender, broker.vaultID),
                loan_broker::kLoanBrokerId(broker.brokerID),
                loan_broker::kDebtMaximum(debtMaximum),
                Fee(env.current()->fees().base * 2));
            env.close();

            env(set(borrower, broker.brokerID, xrpAsset(principalRequest).value()),
                kCounterparty(lender),
                kInterestRate(interestRate),
                kPaymentTotal(paymentTotal),
                kPaymentInterval(paymentInterval),
                Sig(sfCounterpartySignature, lender),
                Fee(env.current()->fees().base * 2),
                Ter(expected));
            env.close();
        };

        Number const oneDrop = xrpAsset(1).value();
        {
            testcase("whole-life: LoanSet AssetsMaximum guard checks interestDue headroom");
            // Guard rejects when there's not quite enough headroom for the
            // interest.
            runVaultGuard(all_, interestDueCash - oneDrop, tecLIMIT_EXCEEDED);
            // Guard accepts at the exact boundary.
            runVaultGuard(all_, interestDueCash, tesSUCCESS);
        }

        {
            testcase("cash-basis: LoanSet AssetsMaximum guard ignores interestDue headroom");
            // Even far less headroom than interestDue still succeeds, since
            // cash-basis origination never adds interest to AssetsTotal.
            runVaultGuard(all_ | featureLendingProtocolV1_1, oneDrop, tesSUCCESS);
        }

        // DebtMaximum guard: cash-basis projects principal-only DebtTotal;
        // whole-life projects principal + interestDue.
        for (auto const cashBasis : {true, false})
        {
            testcase(
                std::string("LoanSet DebtMaximum guard (") +
                (cashBasis ? "cash-basis)" : "whole-life)"));
            auto const features = cashBasis ? all_ | featureLendingProtocolV1_1 : all_;
            Number const newDebtTotal =
                principalOutstandingCash + (cashBasis ? Number{} : interestDueCash);
            runBrokerGuard(features, newDebtTotal - oneDrop, tecLIMIT_EXCEEDED);
            runBrokerGuard(features, newDebtTotal, tesSUCCESS);
        }
    }

    // 2. LoanPay: regular, late, overpayment, and full-payment types.
    // Assert Vault.AssetsTotal/LoanBroker.DebtTotal deltas match
    // interestPaid/principalPaid under cash-basis, and cross-check the
    // amendment-disabled run's deltas against the documented whole-life
    // formula (AssetsTotal += valueChange; DebtTotal mirrors the loan's own
    // TotalValueOutstanding delta exactly, since whole-life debt recognition
    // tracks total loan value).
    void
    testCashBasisLoanPay()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;
        using tp = NetClock::time_point;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};

        Number const principalRequest{12'000};
        TenthBips32 const interestRate{percentageToTenthBips(12)};
        std::uint32_t const paymentTotal = 4;
        std::uint32_t const paymentInterval = 600;
        std::uint32_t const gracePeriod = 300;

        struct PaymentDeltas
        {
            Number principalPaid;
            Number assetsTotalDelta;
            Number debtTotalDelta;
            Number totalValueDelta;
        };

        // Sets up a fresh broker + loan, advances time, submits a single
        // payment of the given type/amount, and returns the observed deltas.
        auto runPayment = [&](FeatureBitset features,
                              std::uint32_t loanSetFlags,
                              std::uint32_t payFlags,
                              std::function<void(Env&, tp const&)> const& advanceTime,
                              std::function<STAmount(LoanState const&)> const& paymentAmount) {
            Env env(*this, features);

            Account const lender{"lender"};
            Account const borrower{"borrower"};
            env.fund(XRP(10'000'000), lender, borrower);
            env.close();

            BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

            LoanParameters const loanParams{
                .account = borrower,
                .counter = lender,
                .principalRequest = principalRequest,
                .interest = interestRate,
                .payTotal = paymentTotal,
                .payInterval = paymentInterval,
                .gracePd = gracePeriod,
                .flags = loanSetFlags,
            };

            auto const brokerBeforeLoan = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerBeforeLoan);
            auto const loanSequence = brokerBeforeLoan->at(sfLoanSequence);
            auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

            env(loanParams(env, broker));
            env.close();

            LoanState const state = getCurrentState(env, broker, loanKeylet);

            advanceTime(env, state.startDate);

            auto const vaultBefore = env.le(broker.vaultKeylet());
            auto const brokerBefore = env.le(broker.brokerKeylet());
            auto const loanBefore = env.le(loanKeylet);
            BEAST_EXPECT(vaultBefore && brokerBefore && loanBefore);

            Number const principalBefore = loanBefore->at(sfPrincipalOutstanding);
            Number const totalValueBefore = loanBefore->at(sfTotalValueOutstanding);
            Number const assetsTotalBefore = vaultBefore->at(sfAssetsTotal);
            Number const debtTotalBefore = brokerBefore->at(sfDebtTotal);

            STAmount const amount = paymentAmount(state);
            env(pay(borrower, loanKeylet.key, amount, payFlags), Ter(tesSUCCESS));
            env.close();

            auto const vaultAfter = env.le(broker.vaultKeylet());
            auto const brokerAfter = env.le(broker.brokerKeylet());
            auto const loanAfter = env.le(loanKeylet);
            BEAST_EXPECT(vaultAfter && brokerAfter && loanAfter);

            Number const principalAfter = loanAfter->at(sfPrincipalOutstanding);
            Number const totalValueAfter = loanAfter->at(sfTotalValueOutstanding);
            Number const assetsTotalAfter = vaultAfter->at(sfAssetsTotal);
            Number const debtTotalAfter = brokerAfter->at(sfDebtTotal);

            return PaymentDeltas{
                .principalPaid = principalBefore - principalAfter,
                .assetsTotalDelta = assetsTotalAfter - assetsTotalBefore,
                .debtTotalDelta = debtTotalAfter - debtTotalBefore,
                .totalValueDelta = totalValueAfter - totalValueBefore};
        };

        // Compares the disabled (whole-life) and enabled (cash-basis) runs
        // of the same payment scenario, and asserts the documented
        // relationships between them.
        auto checkScenario = [&](std::string const& label,
                                 PaymentDeltas const& off,
                                 PaymentDeltas const& on) {
            testcase("cash-basis: LoanPay " + label);

            // The loan's own PrincipalOutstanding field is untouched by
            // the amendment.
            BEAST_EXPECTS(
                off.principalPaid == on.principalPaid,
                "principalPaid must be amendment-independent; off=" + to_string(off.principalPaid) +
                    " on=" + to_string(on.principalPaid));

            // Whole-life structural invariant: DebtTotal (which
            // recognizes a loan's full remaining value as debt) must
            // change exactly as the loan's own TotalValueOutstanding
            // does.
            BEAST_EXPECTS(
                off.debtTotalDelta == off.totalValueDelta,
                "whole-life DebtTotal delta must mirror TotalValueOutstanding delta; "
                "debtTotalDelta=" +
                    to_string(off.debtTotalDelta) +
                    " totalValueDelta=" + to_string(off.totalValueDelta));

            // Derive interestPaid from the whole-life run's independent
            // ledger deltas:
            //   assetsTotalDelta_off == valueChange
            //   debtTotalDelta_off == valueChange - (principalPaid + interestPaid)
            // => interestPaid == assetsTotalDelta_off - debtTotalDelta_off - principalPaid
            Number const interestPaid =
                off.assetsTotalDelta - off.debtTotalDelta - off.principalPaid;
            BEAST_EXPECTS(
                interestPaid >= beast::kZero,
                "derived interestPaid must be non-negative: " + to_string(interestPaid));

            BEAST_EXPECTS(
                on.assetsTotalDelta == interestPaid,
                "cash-basis AssetsTotal delta must equal interestPaid; delta=" +
                    to_string(on.assetsTotalDelta) + " interestPaid=" + to_string(interestPaid));
            BEAST_EXPECTS(
                on.debtTotalDelta == -on.principalPaid,
                "cash-basis DebtTotal delta must equal -principalPaid; delta=" +
                    to_string(on.debtTotalDelta) + " principalPaid=" + to_string(on.principalPaid));
        };

        // ---- Regular, on-time payment ----
        {
            auto const noAdvance = [](Env& env, tp const&) { env.close(); };
            auto const regularAmount = [&](LoanState const& state) {
                return STAmount{
                    xrpAsset,
                    roundPeriodicPayment(xrpAsset, state.periodicPayment, state.loanScale) *
                        Number{3, -1} * 5};  // 1.5x, so only a single period is paid
            };

            auto const off = runPayment(all_, 0, 0, noAdvance, regularAmount);
            auto const on =
                runPayment(all_ | featureLendingProtocolV1_1, 0, 0, noAdvance, regularAmount);

            // Regular, on-time payments never change the loan's value beyond
            // normal amortization (production asserts valueChange == 0), so
            // AssetsTotal must be unaffected in the whole-life run.
            BEAST_EXPECTS(
                off.assetsTotalDelta == beast::kZero,
                "regular on-time payment must not change AssetsTotal under whole-life; delta=" +
                    to_string(off.assetsTotalDelta));

            checkScenario("regular payment", off, on);
        }

        // ---- Late payment ----
        {
            auto const advancePastDue = [&](Env& env, tp const& startDate) {
                env.close(startDate + std::chrono::seconds(paymentInterval + 1));
            };
            auto const lateAmount = [&](LoanState const& state) {
                return STAmount{
                    xrpAsset,
                    roundPeriodicPayment(xrpAsset, state.periodicPayment, state.loanScale) *
                        Number{3}};  // generous; excess is not withdrawn
            };

            auto const off = runPayment(all_, 0, tfLoanLatePayment, advancePastDue, lateAmount);
            auto const on = runPayment(
                all_ | featureLendingProtocolV1_1,
                0,
                tfLoanLatePayment,
                advancePastDue,
                lateAmount);

            checkScenario("late payment", off, on);
        }

        // ---- Overpayment ----
        {
            auto const noAdvance = [](Env& env, tp const&) { env.close(); };
            auto const overpayAmount = [&](LoanState const& state) {
                // One regular period, plus a generous extra principal
                // paydown.
                return STAmount{
                    xrpAsset,
                    roundPeriodicPayment(xrpAsset, state.periodicPayment, state.loanScale) +
                        xrpAsset(2'000).value()};
            };

            auto const off =
                runPayment(all_, tfLoanOverpayment, tfLoanOverpayment, noAdvance, overpayAmount);
            auto const on = runPayment(
                all_ | featureLendingProtocolV1_1,
                tfLoanOverpayment,
                tfLoanOverpayment,
                noAdvance,
                overpayAmount);

            checkScenario("overpayment", off, on);
        }

        // ---- Full payment ----
        {
            auto const noAdvance = [](Env& env, tp const&) { env.close(); };
            auto const fullAmount = [&](LoanState const&) {
                // Generously large: full payment only ever consumes exactly
                // what's due (principal + accrued interest; close fee/
                // prepayment penalty are 0 here), excess is not withdrawn.
                return STAmount{xrpAsset, xrpAsset(principalRequest).value() * Number{2}};
            };

            auto const off = runPayment(all_, 0, tfLoanFullPayment, noAdvance, fullAmount);
            auto const on = runPayment(
                all_ | featureLendingProtocolV1_1, 0, tfLoanFullPayment, noAdvance, fullAmount);

            checkScenario("full payment", off, on);
        }
    }

    // 3. LoanManage: impair, unimpair, and default.
    void
    testCashBasisLoanManage()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{percentageToTenthBips(10)},
            .coverDeposit = 5'000,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{percentageToTenthBips(25)}};

        Number const principalRequest{10'000};
        TenthBips32 const interestRate{percentageToTenthBips(12)};
        std::uint32_t const paymentTotal = 4;
        std::uint32_t const paymentInterval = 600;
        std::uint32_t const gracePeriod = 60;

        auto setupLoan = [&](Env& env) {
            Account const lender{"lender"};
            Account const borrower{"borrower"};
            env.fund(XRP(10'000'000), lender, borrower);
            env.close();

            BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

            LoanParameters const loanParams{
                .account = borrower,
                .counter = lender,
                .principalRequest = principalRequest,
                .interest = interestRate,
                .payTotal = paymentTotal,
                .payInterval = paymentInterval,
                .gracePd = gracePeriod,
            };

            auto const brokerBeforeLoan = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerBeforeLoan);
            auto const loanSequence = brokerBeforeLoan->at(sfLoanSequence);
            auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

            env(loanParams(env, broker));
            env.close();

            return std::make_tuple(broker, loanKeylet, lender, borrower);
        };

        // ---- impair / unimpair ----
        auto runImpairUnimpair = [&](FeatureBitset features) {
            Env env(*this, features);
            auto const [broker, loanKeylet, lender, borrower] = setupLoan(env);

            auto const loanBefore = env.le(loanKeylet);
            BEAST_EXPECT(loanBefore);
            Number const principalOutstanding = loanBefore->at(sfPrincipalOutstanding);
            Number const totalValueOutstanding = loanBefore->at(sfTotalValueOutstanding);
            Number const managementFeeOutstanding = loanBefore->at(sfManagementFeeOutstanding);

            Number const expectedExposure =
                env.current()->rules().enabled(featureLendingProtocolV1_1)
                ? principalOutstanding
                : totalValueOutstanding - managementFeeOutstanding;

            auto const vaultBeforeImpair = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultBeforeImpair);
            Number const lossBefore = vaultBeforeImpair->at(sfLossUnrealized);

            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
            env.close();

            auto const vaultAfterImpair = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultAfterImpair);
            Number const impairDelta = Number(vaultAfterImpair->at(sfLossUnrealized)) - lossBefore;

            env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tesSUCCESS));
            env.close();

            auto const vaultAfterUnimpair = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultAfterUnimpair);
            Number const netDelta = Number(vaultAfterUnimpair->at(sfLossUnrealized)) - lossBefore;

            return std::make_tuple(expectedExposure, impairDelta, netDelta);
        };

        for (auto const features : {all_ | featureLendingProtocolV1_1, all_})
        {
            testcase(
                std::string("cash-basis: LoanManage impair/unimpair (") +
                (features[featureLendingProtocolV1_1] ? "enabled)" : "disabled)"));
            auto const [expectedExposure, impairDelta, netDelta] = runImpairUnimpair(features);

            BEAST_EXPECTS(
                impairDelta == expectedExposure,
                "impair must add loanVaultExposure to LossUnrealized; delta=" +
                    to_string(impairDelta) + " expected=" + to_string(expectedExposure));
            BEAST_EXPECTS(
                netDelta == beast::kZero,
                "unimpair must be an exact reversal of impair; net=" + to_string(netDelta));
        }

        // ---- impair, then default ----
        auto runDefault = [&](FeatureBitset features) {
            Env env(*this, features);
            auto const [broker, loanKeylet, lender, borrower] = setupLoan(env);

            auto const loanBeforeImpair = env.le(loanKeylet);
            BEAST_EXPECT(loanBeforeImpair);
            Number const principalOutstanding = loanBeforeImpair->at(sfPrincipalOutstanding);
            Number const totalValueOutstanding = loanBeforeImpair->at(sfTotalValueOutstanding);
            Number const managementFeeOutstanding =
                loanBeforeImpair->at(sfManagementFeeOutstanding);

            Number const expectedExposure =
                env.current()->rules().enabled(featureLendingProtocolV1_1)
                ? principalOutstanding
                : totalValueOutstanding - managementFeeOutstanding;

            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
            env.close();

            LoanState const state = getCurrentState(env, broker, loanKeylet);
            env.close(
                state.startDate + std::chrono::seconds(paymentInterval) +
                std::chrono::seconds(gracePeriod) + 60s);

            auto const vaultBefore = env.le(broker.vaultKeylet());
            auto const brokerBefore = env.le(broker.brokerKeylet());
            BEAST_EXPECT(vaultBefore && brokerBefore);
            Number const assetsTotalBefore = vaultBefore->at(sfAssetsTotal);
            Number const debtTotalBefore = brokerBefore->at(sfDebtTotal);
            Number const lossBefore = vaultBefore->at(sfLossUnrealized);
            Number const coverAvailableBefore = brokerBefore->at(sfCoverAvailable);

            env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
            env.close();

            auto const vaultAfter = env.le(broker.vaultKeylet());
            auto const brokerAfter = env.le(broker.brokerKeylet());
            BEAST_EXPECT(vaultAfter && brokerAfter);
            Number const assetsTotalDelta =
                Number(vaultAfter->at(sfAssetsTotal)) - assetsTotalBefore;
            Number const debtTotalDelta = Number(brokerAfter->at(sfDebtTotal)) - debtTotalBefore;
            Number const lossDelta = Number(vaultAfter->at(sfLossUnrealized)) - lossBefore;
            Number const coverAvailableDelta =
                Number(brokerAfter->at(sfCoverAvailable)) - coverAvailableBefore;

            Number const defaultCovered = -coverAvailableDelta;
            Number const vaultDefaultAmount = expectedExposure - defaultCovered;

            return std::make_tuple(
                expectedExposure, assetsTotalDelta, debtTotalDelta, lossDelta, vaultDefaultAmount);
        };

        for (auto const features : {all_ | featureLendingProtocolV1_1, all_})
        {
            testcase(
                std::string("cash-basis: LoanManage default (") +
                (features[featureLendingProtocolV1_1] ? "enabled)" : "disabled)"));
            auto const
                [expectedExposure,
                 assetsTotalDelta,
                 debtTotalDelta,
                 lossDelta,
                 vaultDefaultAmount] = runDefault(features);

            BEAST_EXPECTS(
                debtTotalDelta == -expectedExposure,
                "default must reduce DebtTotal by the unified default amount; delta=" +
                    to_string(debtTotalDelta) + " expected=" + to_string(expectedExposure));
            BEAST_EXPECTS(
                lossDelta == -expectedExposure,
                "default must reverse the earlier impair's LossUnrealized exactly; delta=" +
                    to_string(lossDelta) + " expected=" + to_string(expectedExposure));
            BEAST_EXPECTS(
                assetsTotalDelta == -vaultDefaultAmount,
                "default must reduce AssetsTotal by (defaultAmount - defaultCovered); delta=" +
                    to_string(assetsTotalDelta) + " expected=" + to_string(-vaultDefaultAmount));
        }
    }

    // 3b. LEVersion regression: a Vault created before featureLendingProtocolV1_1
    // activates (LEVersion absent) must keep whole-life (accrual) accounting
    // forever, even after the amendment is later enabled -- the switch is
    // per-Vault (LEVersion == VaultVersion::CashBasis), not a single global amendment
    // flag.
    void
    testLegacyVaultKeepsAccrualAfterAmendmentEnabled()
    {
        testcase("LEVersion: legacy vault keeps accrual after amendment enabled");

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{percentageToTenthBips(10)},
            .coverDeposit = 5'000,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{percentageToTenthBips(25)}};

        Number const principalRequest{10'000};
        TenthBips32 const interestRate{percentageToTenthBips(12)};
        std::uint32_t const paymentTotal = 4;
        std::uint32_t const paymentInterval = 600;
        std::uint32_t const gracePeriod = 60;

        // Amendment disabled at Vault creation time: LEVersion stays absent.
        Env env(*this, all_);

        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(10'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        {
            auto const vaultSle = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultSle);
            BEAST_EXPECT(!vaultSle->isFieldPresent(sfLEVersion));
        }

        // Now enable the amendment -- production dispatch must still treat
        // this specific Vault as accrual-basis, since its LEVersion is
        // (and remains) absent.
        env.enableFeature(featureLendingProtocolV1_1);
        env.close();

        LoanParameters const loanParams{
            .account = borrower,
            .counter = lender,
            .principalRequest = principalRequest,
            .interest = interestRate,
            .payTotal = paymentTotal,
            .payInterval = paymentInterval,
            .gracePd = gracePeriod,
        };

        auto const brokerBeforeLoan = env.le(broker.brokerKeylet());
        BEAST_EXPECT(brokerBeforeLoan);
        auto const loanSequence = brokerBeforeLoan->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

        // ---- LoanSet origination: whole-life formulas expected ----
        auto const vaultBeforeSet = env.le(broker.vaultKeylet());
        auto const brokerBeforeSet = env.le(broker.brokerKeylet());
        BEAST_EXPECT(vaultBeforeSet && brokerBeforeSet);
        Number const assetsTotalBeforeSet = vaultBeforeSet->at(sfAssetsTotal);
        Number const debtTotalBeforeSet = brokerBeforeSet->at(sfDebtTotal);

        env(loanParams(env, broker));
        env.close();

        auto const loanAfterSet = env.le(loanKeylet);
        BEAST_EXPECT(loanAfterSet);
        Number const principalOutstanding = loanAfterSet->at(sfPrincipalOutstanding);
        Number const totalValueOutstanding = loanAfterSet->at(sfTotalValueOutstanding);
        Number const interestDue = totalValueOutstanding - principalOutstanding;
        BEAST_EXPECT(interestDue > beast::kZero);

        auto const vaultAfterSet = env.le(broker.vaultKeylet());
        auto const brokerAfterSet = env.le(broker.brokerKeylet());
        BEAST_EXPECT(vaultAfterSet && brokerAfterSet);
        Number const assetsTotalDeltaSet =
            Number(vaultAfterSet->at(sfAssetsTotal)) - assetsTotalBeforeSet;
        Number const debtTotalDeltaSet =
            Number(brokerAfterSet->at(sfDebtTotal)) - debtTotalBeforeSet;

        BEAST_EXPECTS(
            assetsTotalDeltaSet == interestDue,
            "legacy vault origination must still add interestDue to AssetsTotal; delta=" +
                to_string(assetsTotalDeltaSet) + " interestDue=" + to_string(interestDue));
        BEAST_EXPECTS(
            debtTotalDeltaSet == principalOutstanding + interestDue,
            "legacy vault origination must still add principal+interest to DebtTotal; delta=" +
                to_string(debtTotalDeltaSet));

        LoanState const state = getCurrentState(env, broker, loanKeylet);
        env.close();

        // ---- LoanPay: whole-life formulas expected ----
        auto const vaultBeforePay = env.le(broker.vaultKeylet());
        auto const brokerBeforePay = env.le(broker.brokerKeylet());
        auto const loanBeforePay = env.le(loanKeylet);
        BEAST_EXPECT(vaultBeforePay && brokerBeforePay && loanBeforePay);
        Number const totalValueBeforePay = loanBeforePay->at(sfTotalValueOutstanding);
        Number const assetsTotalBeforePay = vaultBeforePay->at(sfAssetsTotal);
        Number const debtTotalBeforePay = brokerBeforePay->at(sfDebtTotal);

        STAmount const paymentAmount{
            xrpAsset, roundPeriodicPayment(xrpAsset, state.periodicPayment, state.loanScale)};
        env(pay(borrower, loanKeylet.key, paymentAmount), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfterPay = env.le(broker.vaultKeylet());
        auto const brokerAfterPay = env.le(broker.brokerKeylet());
        auto const loanAfterPay = env.le(loanKeylet);
        BEAST_EXPECT(vaultAfterPay && brokerAfterPay && loanAfterPay);
        Number const totalValueAfterPay = loanAfterPay->at(sfTotalValueOutstanding);
        Number const assetsTotalDeltaPay =
            Number(vaultAfterPay->at(sfAssetsTotal)) - assetsTotalBeforePay;
        Number const debtTotalDeltaPay =
            Number(brokerAfterPay->at(sfDebtTotal)) - debtTotalBeforePay;
        Number const totalValueDeltaPay = totalValueAfterPay - totalValueBeforePay;

        // A regular, on-time payment has valueChange == 0, so whole-life
        // AssetsTotal is untouched and DebtTotal mirrors TotalValueOutstanding.
        BEAST_EXPECTS(
            assetsTotalDeltaPay == beast::kZero,
            "legacy vault regular payment must not change AssetsTotal; delta=" +
                to_string(assetsTotalDeltaPay));
        BEAST_EXPECTS(
            debtTotalDeltaPay == totalValueDeltaPay,
            "legacy vault DebtTotal delta must mirror TotalValueOutstanding delta; "
            "debtTotalDelta=" +
                to_string(debtTotalDeltaPay) + " totalValueDelta=" + to_string(totalValueDeltaPay));

        // ---- LoanManage: impair, then default -- whole-life exposure expected ----
        auto const loanBeforeImpair = env.le(loanKeylet);
        BEAST_EXPECT(loanBeforeImpair);
        Number const totalValueBeforeImpair = loanBeforeImpair->at(sfTotalValueOutstanding);
        Number const managementFeeBeforeImpair = loanBeforeImpair->at(sfManagementFeeOutstanding);
        Number const expectedExposure = totalValueBeforeImpair - managementFeeBeforeImpair;

        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();

        LoanState const stateAtImpair = getCurrentState(env, broker, loanKeylet);
        env.close(
            stateAtImpair.startDate + std::chrono::seconds(paymentInterval) +
            std::chrono::seconds(gracePeriod) + 60s);

        auto const vaultBeforeDefault = env.le(broker.vaultKeylet());
        auto const brokerBeforeDefault = env.le(broker.brokerKeylet());
        BEAST_EXPECT(vaultBeforeDefault && brokerBeforeDefault);
        Number const debtTotalBeforeDefault = brokerBeforeDefault->at(sfDebtTotal);
        Number const lossBeforeDefault = vaultBeforeDefault->at(sfLossUnrealized);

        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfterDefault = env.le(broker.vaultKeylet());
        auto const brokerAfterDefault = env.le(broker.brokerKeylet());
        BEAST_EXPECT(vaultAfterDefault && brokerAfterDefault);
        Number const debtTotalDeltaDefault =
            Number(brokerAfterDefault->at(sfDebtTotal)) - debtTotalBeforeDefault;
        Number const lossDeltaDefault =
            Number(vaultAfterDefault->at(sfLossUnrealized)) - lossBeforeDefault;

        BEAST_EXPECTS(
            debtTotalDeltaDefault == -expectedExposure,
            "legacy vault default must reduce DebtTotal by whole-life exposure; delta=" +
                to_string(debtTotalDeltaDefault) + " expected=" + to_string(expectedExposure));
        BEAST_EXPECTS(
            lossDeltaDefault == -expectedExposure,
            "legacy vault default must reverse the earlier impair's LossUnrealized exactly; "
            "delta=" +
                to_string(lossDeltaDefault) + " expected=" + to_string(expectedExposure));

        // Confirm the Vault's LEVersion truly never got set, throughout.
        {
            auto const vaultSle = env.le(broker.vaultKeylet());
            BEAST_EXPECT(vaultSle);
            BEAST_EXPECT(!vaultSle->isFieldPresent(sfLEVersion));
            BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::Legacy);
        }
    }

    // 4. End-to-end trajectory: LoanSet -> 2 LoanPays -> LoanManage(default),
    // entirely under the amendment, with independently hand-computed
    // expected AssetsTotal/DebtTotal/LossUnrealized/CoverAvailable values at
    // each step. 0% interest keeps the arithmetic exact and tractable; the
    // divergence from whole-life accounting is already covered directly by
    // testCashBasisLoanSetOrigination/LoanPay/LoanManage above, so this test
    // focuses purely on an independent, from-scratch trajectory check.
    void
    testCashBasisEndToEndTrajectory()
    {
        testcase("cash-basis: end-to-end trajectory");

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000, .managementFeeRate = TenthBips16{0}};

        Env env(*this, all_ | featureLendingProtocolV1_1);

        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(10'000'000), lender, borrower);
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        // Hand computation (all values in XRP, drops == 1e-6 XRP):
        //   Vault:  AssetsTotal starts at 100'000 (the deposit).
        //   Broker: DebtTotal starts at 0, CoverAvailable starts at 1'000
        //           (BrokerParameters::defaults().coverDeposit).
        auto const vaultKeylet = broker.vaultKeylet();
        auto const brokerKeylet = broker.brokerKeylet();

        // All the "human XRP unit" constants below (e.g. `100'000`) are
        // converted to raw native (drops) values via xrpAsset(...), since
        // that's how the ledger fields are actually denominated.
        auto const checkVaultBroker = [&](Number const& assetsTotalUnits,
                                          Number const& debtTotalUnits,
                                          Number const& lossUnrealizedUnits,
                                          Number const& coverAvailableUnits,
                                          char const* step) {
            Number const assetsTotal = xrpAsset(assetsTotalUnits).value();
            Number const debtTotal = xrpAsset(debtTotalUnits).value();
            Number const lossUnrealized = xrpAsset(lossUnrealizedUnits).value();
            Number const coverAvailable = xrpAsset(coverAvailableUnits).value();

            auto const vaultSle = env.le(vaultKeylet);
            auto const brokerSle = env.le(brokerKeylet);
            BEAST_EXPECT(vaultSle && brokerSle);
            BEAST_EXPECTS(
                vaultSle->at(sfAssetsTotal) == assetsTotal,
                std::string(step) + ": AssetsTotal expected " + to_string(assetsTotal) + " got " +
                    to_string(Number(vaultSle->at(sfAssetsTotal))));
            BEAST_EXPECTS(
                brokerSle->at(sfDebtTotal) == debtTotal,
                std::string(step) + ": DebtTotal expected " + to_string(debtTotal) + " got " +
                    to_string(Number(brokerSle->at(sfDebtTotal))));
            BEAST_EXPECTS(
                vaultSle->at(sfLossUnrealized) == lossUnrealized,
                std::string(step) + ": LossUnrealized expected " + to_string(lossUnrealized) +
                    " got " + to_string(Number(vaultSle->at(sfLossUnrealized))));
            BEAST_EXPECTS(
                brokerSle->at(sfCoverAvailable) == coverAvailable,
                std::string(step) + ": CoverAvailable expected " + to_string(coverAvailable) +
                    " got " + to_string(Number(brokerSle->at(sfCoverAvailable))));
        };

        checkVaultBroker(100'000, 0, 0, 1'000, "before LoanSet");

        // Loan: principal=1200, 0% interest, 12 payments of 100 each, no fees.
        Number const principalRequest{1'200};
        std::uint32_t const paymentTotal = 12;
        std::uint32_t const paymentInterval = 600;
        std::uint32_t const gracePeriod = 60;

        auto const brokerBeforeLoan = env.le(brokerKeylet);
        BEAST_EXPECT(brokerBeforeLoan);
        auto const loanSequence = brokerBeforeLoan->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

        LoanParameters const loanParams{
            .account = borrower,
            .counter = lender,
            .principalRequest = principalRequest,
            .interest = TenthBips32{0},
            .payTotal = paymentTotal,
            .payInterval = paymentInterval,
            .gracePd = gracePeriod,
        };
        env(loanParams(env, broker));
        env.close();

        // Origination (cash-basis): AssetsTotal += 0, DebtTotal += principal.
        checkVaultBroker(100'000, 1'200, 0, 1'000, "after LoanSet");

        LoanState const state = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(state.periodicPayment == xrpAsset(100).value());

        // Payment 1: principalPaid=100, interestPaid=0.
        //   AssetsTotal += 0; DebtTotal -= 100.
        env(pay(borrower, loanKeylet.key, xrpAsset(100).value()), Ter(tesSUCCESS));
        env.close();
        checkVaultBroker(100'000, 1'100, 0, 1'000, "after payment 1");

        // Payment 2: same as above.
        env(pay(borrower, loanKeylet.key, xrpAsset(100).value()), Ter(tesSUCCESS));
        env.close();
        checkVaultBroker(100'000, 1'000, 0, 1'000, "after payment 2");

        // Default (no impair): principalOutstanding remaining is 1'000.
        //   totalDefaultAmount (cash-basis) = PrincipalOutstanding = 1'000.
        //   minimumCover = DebtTotal(1'000) * coverRateMin(10%) = 100.
        //   covered = min(minimumCover * coverRateLiquidation(25%), totalDefaultAmount)
        //           = min(25, 1'000) = 25.
        //   defaultCovered = min(covered, CoverAvailable(1'000)) = 25.
        //   vaultDefaultAmount = 1'000 - 25 = 975.
        //   DebtTotal -= 1'000 -> 0.  CoverAvailable -= 25 -> 975.
        //   AssetsTotal -= 975 -> 99'025.  LossUnrealized unaffected (never impaired).
        auto const loanBeforeDefault = env.le(loanKeylet);
        BEAST_EXPECT(loanBeforeDefault);
        BEAST_EXPECT(
            Number(loanBeforeDefault->at(sfPrincipalOutstanding)) == xrpAsset(1'000).value());

        env.close(state.startDate + std::chrono::seconds((3 * paymentInterval) + gracePeriod) + 1s);

        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();

        checkVaultBroker(99'025, 0, 0, 975, "after LoanManage(default)");
    }

public:
    void
    run() override
    {
        testCashBasisLoanSetOrigination();
        testCashBasisLoanPay();
        testCashBasisLoanManage();
        testLegacyVaultKeepsAccrualAfterAmendmentEnabled();
        testCashBasisEndToEndTrajectory();
    }
};

BEAST_DEFINE_TESTSUITE(LoanCashBasis, tx, xrpl);

}  // namespace xrpl::test
