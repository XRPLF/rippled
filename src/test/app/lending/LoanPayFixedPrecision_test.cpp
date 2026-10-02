#include <test/app/lending/LoanPayFixedPrecisionBase.h>
#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace xrpl {

// LoanPay-focused FixedPrecision tests: AssetsDeployed tracking through
// payments, overpayment and early payoff, and the coarsened-vault scenarios
// where repeated late-interest payments push a FixedPrecision vault's live
// Scale past its base grid.
class LoanPayFixedPrecision_test : public LoanPayFixedPrecisionBase
{
    void
    testLendingAssetsDeployedPayments()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase("Lending: LoanPay reduces AssetsDeployed by exactly the principal paid");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{1'000});
        auto const loanKeylet = openLoan(env, fixture, Number{300}, 2);

        env(pay(depositor, loanKeylet.key, asset(150).value()));
        env.close();

        auto sle = expectVault(env, fixture.vaultKeylet, {.assetsDeployed = Number{150}});
        if (!sle)
            return;
        checkVaultLoanSums(env, fixture, {loanKeylet}, "first payment");

        env(pay(depositor, loanKeylet.key, asset(150).value()));
        env.close();

        sle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(sle->at(sfAssetsDeployed) == beast::kZero);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == getAssetsTotal(sle));
        BEAST_EXPECT(sle->at(sfAssetsAvailable) == Number{1'000});
        BEAST_EXPECT(sle->at(sfAssetsTotal) == Number{1'000});
        checkVaultLoanSums(env, fixture, {loanKeylet}, "final payment");
    }

    void
    testLendingAssetsDeployedInterestPayoff()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: FixedPrecision vault with interest -- AssetsDeployed tracks "
            "PrincipalOutstanding "
            "through payoff");

        // Extra depositor funds cover the per-period rounding buffer.
        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.depositorTrust = 2'100, .ownerTrust = 2'000});

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{2'000});
        auto const loanKeylet = openLoan(
            env,
            fixture,
            Number{1'000},
            3,
            percentageToTenthBips(5),
            0,
            3'600);  // generous 1-hour interval: payments below must not drift late

        auto const loanSle0 = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle0))
            return;
        // sfPeriodicPayment is the exact annuity payment; the amount due each period
        // is that value rounded Upward to the loan scale.
        Number const periodicPayment = loanSle0->at(sfPeriodicPayment);
        std::int32_t const loanScale = loanSle0->at(sfLoanScale);
        Number const periodicPaymentDue = roundPeriodicPayment(asset, periodicPayment, loanScale);

        auto const vaultSle0 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        // The vault pseudo-account's own balance, tracked apart from
        // AssetsAvailable to confirm they move together.
        Account const vaultAccount("vault", vaultSle0->at(sfAccount));
        env.memoize(vaultAccount);

        Number prevPrincipal = loanSle0->at(sfPrincipalOutstanding);
        Number prevAssetsDeployed = vaultSle0->at(sfAssetsDeployed);
        Number prevAvailable = vaultSle0->at(sfAssetsAvailable);
        Number prevVaultBalance = env.balance(vaultAccount, asset).value();
        BEAST_EXPECT(prevAssetsDeployed == prevPrincipal);

        for (std::uint32_t i = 0; i < 3; ++i)
        {
            env(pay(depositor, loanKeylet.key, asset(periodicPaymentDue).value()));
            env.close();

            auto const loanSle = env.le(loanKeylet);
            auto const vaultSle = env.le(fixture.vaultKeylet);
            if (!BEAST_EXPECT(loanSle) || !BEAST_EXPECT(vaultSle))
                return;

            Number const principal = loanSle->at(sfPrincipalOutstanding);
            Number const assetsDeployed = vaultSle->at(sfAssetsDeployed);
            Number const available = vaultSle->at(sfAssetsAvailable);
            Number const vaultBalance = env.balance(vaultAccount, asset).value();

            // AssetsDeployed tracks the single Loan's PrincipalOutstanding.
            BEAST_EXPECT(assetsDeployed == principal);
            BEAST_EXPECT(prevAssetsDeployed - assetsDeployed == prevPrincipal - principal);
            BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == getAssetsTotal(vaultSle));
            BEAST_EXPECT(available - prevAvailable == vaultBalance - prevVaultBalance);
            checkVaultLoanSums(
                env, fixture, {loanKeylet}, "interest payoff period " + std::to_string(i));

            prevPrincipal = principal;
            prevAssetsDeployed = assetsDeployed;
            prevAvailable = available;
            prevVaultBalance = vaultBalance;
        }

        BEAST_EXPECT(prevAssetsDeployed == beast::kZero);
        BEAST_EXPECT(prevPrincipal == beast::kZero);
    }

    void
    testLendingAssetsDeployedOverpayment()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: LoanPay overpayment reduces AssetsDeployed by the full principal reduction");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{1'000});
        // tfLoanOverpayment marks the loan as accepting overpayments.
        auto const loanKeylet =
            openLoan(env, fixture, Number{500}, 5, TenthBips32(0), tfLoanOverpayment);

        auto const loanSleBefore = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        Number const principalBefore = loanSleBefore->at(sfPrincipalOutstanding);

        auto vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        Number const assetsDeployedBefore = vaultSle->at(sfAssetsDeployed);
        BEAST_EXPECT(assetsDeployedBefore == principalBefore);

        // A periodic payment here would be 100; overpay well beyond that.
        env(pay(depositor, loanKeylet.key, asset(300).value(), tfLoanOverpayment));
        env.close();

        auto const loanSleAfter = env.le(loanKeylet);
        vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(loanSleAfter) || !BEAST_EXPECT(vaultSle))
            return;
        Number const principalAfter = loanSleAfter->at(sfPrincipalOutstanding);
        Number const assetsDeployedAfter = vaultSle->at(sfAssetsDeployed);

        // The overpayment paid down more than one periodic payment.
        BEAST_EXPECT(principalBefore - principalAfter > Number{100});
        BEAST_EXPECT(assetsDeployedAfter == principalAfter);
        BEAST_EXPECT(
            assetsDeployedBefore - assetsDeployedAfter == principalBefore - principalAfter);
        BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == getAssetsTotal(vaultSle));
        checkVaultLoanSums(env, fixture, {loanKeylet}, "overpayment");
    }

    void
    testLendingAssetsDeployedEarlyFullPayoff()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: tfLoanFullPayment zeroes one Loan's contribution to AssetsDeployed, leaving "
            "another loan's principal");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{1'000});
        auto const loanAKeylet = openLoan(env, fixture, Number{300}, 3);
        auto const loanBKeylet = openLoan(env, fixture, Number{200}, 2);

        auto vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(vaultSle->at(sfAssetsDeployed) == Number{500});
        checkVaultLoanSums(env, fixture, {loanAKeylet, loanBKeylet}, "early payoff, before payoff");

        auto const loanASle = env.le(loanAKeylet);
        if (!BEAST_EXPECT(loanASle))
            return;
        Number const principalA = loanASle->at(sfPrincipalOutstanding);

        env(pay(depositor, loanAKeylet.key, asset(principalA).value(), tfLoanFullPayment));
        env.close();

        auto const loanASleAfter = env.le(loanAKeylet);
        vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(loanASleAfter) || !BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(loanASleAfter->at(sfPrincipalOutstanding) == beast::kZero);
        BEAST_EXPECT(loanASleAfter->at(sfPaymentRemaining) == 0);
        BEAST_EXPECT(vaultSle->at(sfAssetsDeployed) == Number{200});
        BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == getAssetsTotal(vaultSle));
        checkVaultLoanSums(env, fixture, {loanAKeylet, loanBKeylet}, "early payoff, after payoff");
    }

    void
    testLendingAssetsDeployedIntegralAsset()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: MPT (integral asset) FixedPrecision vault -- AssetsDeployed tracks "
            "PrincipalOutstanding through payoff");

        // Extra depositor funds cover the per-period rounding buffer.
        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupMpt(env, {.maxAmt = 100'000, .withDepositor = true, .holderFunds = 2'100});

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{2'000});
        auto const loanKeylet = openLoan(
            env,
            fixture,
            Number{1'000},
            3,
            percentageToTenthBips(5),
            0,
            3'600);  // generous 1-hour interval: payments below must not drift late

        auto const loanSle0 = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle0))
            return;
        // As in testLendingAssetsDeployedInterestPayoff: the amount due each period is
        // the annuity payment rounded Upward to the loan scale.
        Number const periodicPayment = loanSle0->at(sfPeriodicPayment);
        std::int32_t const loanScale = loanSle0->at(sfLoanScale);
        Number const periodicPaymentDue = roundPeriodicPayment(asset, periodicPayment, loanScale);

        auto const vaultSle0 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        Account const vaultAccount("vault", vaultSle0->at(sfAccount));
        env.memoize(vaultAccount);

        Number prevPrincipal = loanSle0->at(sfPrincipalOutstanding);
        Number prevAssetsDeployed = vaultSle0->at(sfAssetsDeployed);
        Number prevAvailable = vaultSle0->at(sfAssetsAvailable);
        Number prevVaultBalance = env.balance(vaultAccount, asset).value();
        BEAST_EXPECT(prevAssetsDeployed == prevPrincipal);

        for (std::uint32_t i = 0; i < 3; ++i)
        {
            env(pay(depositor, loanKeylet.key, asset(periodicPaymentDue).value()));
            env.close();

            auto const loanSle = env.le(loanKeylet);
            auto const vaultSle = env.le(fixture.vaultKeylet);
            if (!BEAST_EXPECT(loanSle) || !BEAST_EXPECT(vaultSle))
                return;

            Number const principal = loanSle->at(sfPrincipalOutstanding);
            Number const assetsDeployed = vaultSle->at(sfAssetsDeployed);
            Number const available = vaultSle->at(sfAssetsAvailable);
            Number const vaultBalance = env.balance(vaultAccount, asset).value();

            BEAST_EXPECT(assetsDeployed == principal);
            BEAST_EXPECT(prevAssetsDeployed - assetsDeployed == prevPrincipal - principal);
            BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == getAssetsTotal(vaultSle));
            BEAST_EXPECT(available - prevAvailable == vaultBalance - prevVaultBalance);
            checkVaultLoanSums(
                env, fixture, {loanKeylet}, "integral asset payoff period " + std::to_string(i));

            prevPrincipal = principal;
            prevAssetsDeployed = assetsDeployed;
            prevAvailable = available;
            prevVaultBalance = vaultBalance;
        }

        BEAST_EXPECT(prevAssetsDeployed == beast::kZero);
        BEAST_EXPECT(prevPrincipal == beast::kZero);
    }

    void
    testLateInterestCoarsensVault()
    {
        using namespace test::jtx;

        testcase("Lending: paid late interest coarsens a FixedPrecision vault's live scale");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        auto const coarsened = coarsenVault(env, owner, depositor, asset);

        auto const vaultSle = env.le(coarsened.fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(getVaultScale(vaultSle) > getVaultBaseScale(vaultSle));
        checkVaultLoanSums(env, coarsened, "testLateInterestCoarsensVault");
    }

    void
    testCoarsenedVaultCreditFlooring()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase("Lending: on a coarsened vault, a payment's vault credit floors at the live unit");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // otherPrincipal is not a multiple of the coarsened live unit (1e-9 at Scale
        // 10; the loan's own scale is 1e-10), so its principal payment floors when
        // credited.
        Number const otherPrincipal{1234'0000000007LL, -10};
        auto const coarsened = coarsenVault(
            env, owner, depositor, asset, {.extraLoans = {ExtraLoan{.principal = otherPrincipal}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& otherLoanKeylet = coarsened.loanKeylets[1];

        auto const vaultSleBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        BEAST_EXPECT(getVaultScale(vaultSleBefore) > getVaultBaseScale(vaultSleBefore));

        auto const loanSleBefore = env.le(otherLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        Number const principalBefore = loanSleBefore->at(sfPrincipalOutstanding);
        auto const before = snapshotVault(env, vaultKeylet, asset);

        // The first installment is overdue by now, but this loan's LateInterestRate
        // is 0, so the late flag adds no penalty.
        env(pay(depositor, otherLoanKeylet.key, asset(otherPrincipal).value(), tfLoanLatePayment));
        env.close();

        auto const vaultSleAfter = env.le(vaultKeylet);
        auto const loanSleAfter = env.le(otherLoanKeylet);
        if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(loanSleAfter))
            return;
        Number const principalAfter = loanSleAfter->at(sfPrincipalOutstanding);
        auto const after = snapshotVault(env, vaultKeylet, asset);
        Number const principalPaid = principalBefore - principalAfter;

        BEAST_EXPECT(
            after.available - before.available == after.vaultBalance - before.vaultBalance);
        BEAST_EXPECT(before.assetsDeployed - after.assetsDeployed == principalPaid);
        // Stored AssetsTotal <= derived, within one live unit.
        Number const derived = getAssetsTotal(vaultSleAfter);
        Number const stored = vaultSleAfter->at(sfAssetsTotal);
        Number const liveUnit{1, getVaultScale(vaultSleAfter)};
        BEAST_EXPECT(stored <= derived);
        BEAST_EXPECT(derived - stored < liveUnit);
        BEAST_EXPECT(Number(vaultSleAfter->at(sfAssetsAvailable)) <= stored);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultCreditFlooring");
    }

    void
    testCoarseningPaymentCreditNeverExceedsPaid()
    {
        using namespace test::jtx;

        testcase(
            "Lending: the payment that coarsens a vault credits no more than principal plus "
            "interest paid");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // depositor is both the vault's sole depositor and the coarsening loan's
        // borrower, and the only transaction between the deposit and the
        // coarsening late payment is the deposit itself (known amount), so the
        // rest of depositor's balance change across coarsenVault is exactly what
        // the late payment moved: principalPaid + interestPaid (this loan has no
        // service fee). Likewise the pseudo-account only ever receives the
        // deposit and that payment's vault credit.
        Number const depositorBalanceBefore = env.balance(depositor, asset).value();
        CoarsenParams const params{};

        auto const coarsened = coarsenVault(env, owner, depositor, asset, params);
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;

        auto const vaultSleAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleAfter))
            return;
        BEAST_EXPECT(getVaultScale(vaultSleAfter) > getVaultBaseScale(vaultSleAfter));

        Number const depositorBalanceAfter = env.balance(depositor, asset).value();
        Number const rawPaid =
            (depositorBalanceBefore - depositorBalanceAfter) - params.depositAmount;

        test::jtx::Account const vaultAccount("vault", vaultSleAfter->at(sfAccount));
        env.memoize(vaultAccount);
        Number const vaultBalanceAfter = env.balance(vaultAccount, asset).value();
        Number const credit = vaultBalanceAfter - params.depositAmount;

        // N1: creditToPosteriorScale used to build the returned STAmount under
        // the caller's ambient (ToNearest) mode instead of roundingMode, so a
        // 17-digit flooredSum - reference could round up and credit more than
        // was actually paid.
        BEAST_EXPECT(credit <= rawPaid);
        BEAST_EXPECT(Number(vaultSleAfter->at(sfAssetsAvailable)) == vaultBalanceAfter);

        checkVaultLoanSums(env, coarsened, "testCoarseningPaymentCreditNeverExceedsPaid");
    }

    void
    testCoarsenedVaultSubUnitRemainderPayments()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: sub-unit remainder payments on a coarsened vault (terminal closes with a "
            "zero credit; non-terminal is rejected)");

        Env env(*this, features());
        auto const accounts = setupCoarsenIou(env);
        auto const& [issuer, owner, depositor, asset] = accounts;
        // dustBorrower holds a small balance so a sub-1e-9 debit registers; see
        // ExtraLoan::borrower.
        Account const dustBorrower{"dustBorrower"};
        fundIouHolder(env, dustBorrower, accounts, 10, 1);

        Number const overpayPrincipal{1'000};
        // Half a live unit once coarsened (1e-9 at Scale 10).
        Number const dust{5, -10};
        // Three payments of 3e-10 each: below the live unit, but non-terminal
        // because two payments remain.
        Number const nonTerminalPrincipal{9, -10};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {
                 ExtraLoan{
                     .principal = overpayPrincipal,
                     .flags = tfLoanOverpayment,
                     .paymentTotal = 2,
                     // Long enough that its first payment is not yet due, so it can be paid as
                     // a plain overpayment.
                     .paymentInterval = 1000 * 24 * 60 * 60},
                 ExtraLoan{
                     .principal = nonTerminalPrincipal,
                     .paymentTotal = 3,
                     .paymentInterval = 1000 * 24 * 60 * 60,
                     .borrower = dustBorrower}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& loanKeylet = coarsened.loanKeylets[1];
        auto const& nonTerminalLoanKeylet = coarsened.loanKeylets[2];

        auto const vaultSle0 = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        BEAST_EXPECT(getVaultScale(vaultSle0) > getVaultBaseScale(vaultSle0));
        Number const liveUnit{1, getVaultScale(vaultSle0)};
        BEAST_EXPECT(dust < liveUnit);

        env(pay(
            depositor, loanKeylet.key, asset(overpayPrincipal - dust).value(), tfLoanOverpayment));
        env.close();

        auto const loanSleMid = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSleMid))
            return;
        Number const principalMid = loanSleMid->at(sfPrincipalOutstanding);
        std::uint32_t const paymentRemainingMid = loanSleMid->at(sfPaymentRemaining);
        BEAST_EXPECT(paymentRemainingMid == 1);
        BEAST_EXPECT(principalMid < liveUnit);
        BEAST_EXPECT(principalMid > beast::kZero);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultSubUnitRemainderPayments (mid)");

        auto const mid = snapshotVault(env, vaultKeylet, asset);
        auto const brokerSleMid = env.le(coarsened.fixture.brokerKeylet);
        if (!BEAST_EXPECT(brokerSleMid))
            return;
        Number const brokerDebtMid = brokerSleMid->at(sfDebtTotal);

        auto const brokerKeylet = coarsened.fixture.brokerKeylet;

        // Terminal payment: the cash credit rounds to zero but the Loan still
        // closes. With no fees, both legs are zero, taking LoanPay's terminal-close
        // path.
        env(pay(depositor, loanKeylet.key, asset(Number{1}).value()), Ter(tesSUCCESS));
        env.close();

        auto const loanSleAfter = env.le(loanKeylet);
        auto const brokerSleAfter = env.le(brokerKeylet);
        if (!BEAST_EXPECT(loanSleAfter) || !BEAST_EXPECT(brokerSleAfter))
            return;

        BEAST_EXPECT(loanSleAfter->at(sfPaymentRemaining) == 0);
        BEAST_EXPECT(loanSleAfter->at(sfPrincipalOutstanding) == beast::kZero);
        auto const after = snapshotVault(env, vaultKeylet, asset);
        Number const brokerDebtAfter = brokerSleAfter->at(sfDebtTotal);

        BEAST_EXPECT(mid.assetsDeployed - after.assetsDeployed == principalMid);
        BEAST_EXPECT(brokerDebtMid - brokerDebtAfter == principalMid);
        // No cash reaches the vault.
        BEAST_EXPECT(after.available == mid.available);
        BEAST_EXPECT(after.vaultBalance == mid.vaultBalance);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultSubUnitRemainderPayments (terminal)");

        // Non-terminal: the first of 3 payments (3e-10) is below the live unit with
        // 2 remaining, so its credit rounds to zero and the payment is rejected.
        auto const nonTerminalLoanBefore = env.le(nonTerminalLoanKeylet);
        if (!BEAST_EXPECT(nonTerminalLoanBefore))
            return;
        BEAST_EXPECT(nonTerminalLoanBefore->at(sfPaymentRemaining) == 3);
        Number const nonTerminalPrincipalBefore = nonTerminalLoanBefore->at(sfPrincipalOutstanding);
        auto const vaultSleBeforeNT2 = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBeforeNT2))
            return;

        // Pay exactly one period's due (nonTerminalPrincipal / 3); a larger amount
        // would sweep all 3 tiny periods and make the payment terminal. The credit
        // only floors if AssetsAvailable itself is coarsened, which the 800-day
        // overdue period arranges.
        env(pay(dustBorrower, nonTerminalLoanKeylet.key, asset(nonTerminalPrincipal / 3).value()),
            Ter(tecPRECISION_LOSS));
        env.close();

        auto const nonTerminalLoanAfter = env.le(nonTerminalLoanKeylet);
        auto const vaultSleAfterNT2 = env.le(vaultKeylet);
        if (!BEAST_EXPECT(nonTerminalLoanAfter) || !BEAST_EXPECT(vaultSleAfterNT2))
            return;
        BEAST_EXPECT(nonTerminalLoanAfter->at(sfPaymentRemaining) == 3);
        BEAST_EXPECT(
            Number(nonTerminalLoanAfter->at(sfPrincipalOutstanding)) == nonTerminalPrincipalBefore);
        BEAST_EXPECT(
            Number(vaultSleAfterNT2->at(sfAssetsAvailable)) ==
            Number(vaultSleBeforeNT2->at(sfAssetsAvailable)));

        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultSubUnitRemainderPayments (non-terminal rejection)");
    }

    void
    testCoarsenedVaultTerminalCloseWithFee()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: terminal close with a zero vault credit but a non-zero fee needs no "
            "exemption");

        Env env(*this, features());
        auto const accounts = setupCoarsenIou(env);
        auto const& [issuer, owner, depositor, asset] = accounts;
        Account const dustBorrower{"dustBorrower"};
        fundIouHolder(env, dustBorrower, accounts, 10, 2);

        // A single-payment loan: the dust principal floors to a zero vault credit,
        // but the flat service fee is non-zero. It is also the terminal payment.
        Number const dustPrincipal{9, -10};
        Number const fee{1};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = dustPrincipal,
                 .paymentTotal = 1,
                 .paymentInterval = 1000 * 24 * 60 * 60,
                 .borrower = dustBorrower,
                 .serviceFee = fee}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& feeLoanKeylet = coarsened.loanKeylets[1];

        auto const vaultSleBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        BEAST_EXPECT(getVaultScale(vaultSleBefore) > getVaultBaseScale(vaultSleBefore));

        auto const before = snapshotVault(env, vaultKeylet, asset);
        Number const ownerBalanceBefore = env.balance(owner, asset).value();

        auto const loanSleBefore = env.le(feeLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        BEAST_EXPECT(loanSleBefore->at(sfPaymentRemaining) == 1);

        // The vault credit is zero but the fee leg is not, so this passes the
        // normal conservation checks, not the both-legs-zero path.
        env(pay(dustBorrower, feeLoanKeylet.key, asset(dustPrincipal + fee).value()));
        env.close();

        auto const loanSleAfter = env.le(feeLoanKeylet);
        auto const vaultSleAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(loanSleAfter) || !BEAST_EXPECT(vaultSleAfter))
            return;
        BEAST_EXPECT(loanSleAfter->at(sfPaymentRemaining) == 0);
        BEAST_EXPECT(loanSleAfter->at(sfPrincipalOutstanding) == beast::kZero);

        auto const after = snapshotVault(env, vaultKeylet, asset);
        Number const ownerBalanceAfter = env.balance(owner, asset).value();

        // No vault credit.
        BEAST_EXPECT(after.available == before.available);
        BEAST_EXPECT(after.vaultBalance == before.vaultBalance);
        BEAST_EXPECT(ownerBalanceAfter - ownerBalanceBefore == fee);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultTerminalCloseWithFee");
    }

    void
    testCoarsenedVaultTerminalCloseFeeToCoverFloorsToZero()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: terminal close with zero vault credit and a fee redirected to cover that "
            "itself floors to zero still succeeds");

        Env env(*this, features());
        auto const accounts = setupCoarsenIou(env);
        auto const& [issuer, owner, depositor, asset] = accounts;
        Account const dustBorrower{"dustBorrower"};
        fundIouHolder(env, dustBorrower, accounts, 10, 2);

        // Both the dust principal and the dust fee are on the vault's base
        // grid, well below CoverAvailable's magnitude, so
        // creditToPosteriorBrokerCoverScale floors the fee credit to zero too:
        // the payment still takes the both-legs-zero terminal-close path, but
        // through the cover branch instead of the owner branch.
        Number const dustPrincipal{9, -10};
        Number const dustFee{1, -10};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = dustPrincipal,
                 .paymentTotal = 1,
                 .paymentInterval = 1000 * 24 * 60 * 60,
                 .borrower = dustBorrower,
                 .serviceFee = dustFee}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& brokerKeylet = coarsened.fixture.brokerKeylet;
        auto const& feeLoanKeylet = coarsened.loanKeylets[1];

        // Deep-freeze the broker owner, now that the vault/broker/loan are
        // already originated, so the upcoming terminal payment's fee cannot be
        // sent directly to them; it is redirected into CoverAvailable instead
        // (sendBrokerFeeToOwner == false).
        env(trust(issuer, asset(0), owner, tfSetFreeze | tfSetDeepFreeze));
        env.close();

        auto const vaultSleBefore = env.le(vaultKeylet);
        auto const brokerSleBefore = env.le(brokerKeylet);
        if (!BEAST_EXPECT(vaultSleBefore) || !BEAST_EXPECT(brokerSleBefore))
            return;
        BEAST_EXPECT(getVaultScale(vaultSleBefore) > getVaultBaseScale(vaultSleBefore));
        Number const coverAvailableBefore = brokerSleBefore->at(sfCoverAvailable);

        auto const before = snapshotVault(env, vaultKeylet, asset);

        auto const loanSleBefore = env.le(feeLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        BEAST_EXPECT(loanSleBefore->at(sfPaymentRemaining) == 1);

        env(pay(dustBorrower, feeLoanKeylet.key, asset(dustPrincipal + dustFee).value()));
        env.close();

        auto const loanSleAfter = env.le(feeLoanKeylet);
        auto const brokerSleAfter = env.le(brokerKeylet);
        if (!BEAST_EXPECT(loanSleAfter) || !BEAST_EXPECT(brokerSleAfter))
            return;
        BEAST_EXPECT(loanSleAfter->at(sfPaymentRemaining) == 0);
        BEAST_EXPECT(loanSleAfter->at(sfPrincipalOutstanding) == beast::kZero);

        auto const after = snapshotVault(env, vaultKeylet, asset);
        Number const coverAvailableAfter = brokerSleAfter->at(sfCoverAvailable);

        // The vault credit leg floors to zero regardless; the cover credit
        // leg is bounded by the dust fee itself (CoverAvailable starts at
        // zero here, so there is no existing grid to floor a sub-unit amount
        // against). Either way the loan still closes.
        BEAST_EXPECT(after.available == before.available);
        BEAST_EXPECT(after.vaultBalance == before.vaultBalance);
        BEAST_EXPECT(coverAvailableAfter - coverAvailableBefore >= beast::kZero);
        BEAST_EXPECT(coverAvailableAfter - coverAvailableBefore <= dustFee);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultTerminalCloseFeeToCoverFloorsToZero");
    }

    void
    testCoarsenedVaultTerminalCloseFeeToCoverNonZero()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: terminal close with zero vault credit and a non-zero fee redirected to "
            "cover moves CoverAvailable and the broker pseudo-account by the same amount");

        Env env(*this, features());
        auto const accounts = setupCoarsenIou(env);
        auto const& [issuer, owner, depositor, asset] = accounts;
        Account const dustBorrower{"dustBorrower"};
        fundIouHolder(env, dustBorrower, accounts, 10, 2);

        Number const dustPrincipal{9, -10};
        Number const fee{1};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = dustPrincipal,
                 .paymentTotal = 1,
                 .paymentInterval = 1000 * 24 * 60 * 60,
                 .borrower = dustBorrower,
                 .serviceFee = fee}}});
        auto const& brokerKeylet = coarsened.fixture.brokerKeylet;
        auto const& feeLoanKeylet = coarsened.loanKeylets[1];

        // Deep-freeze the broker owner, now that the vault/broker/loan are
        // already originated, so the upcoming terminal payment's fee is
        // redirected into CoverAvailable (sendBrokerFeeToOwner == false). The
        // fee itself is large enough not to floor to zero on the broker's
        // cover grid.
        env(trust(issuer, asset(0), owner, tfSetFreeze | tfSetDeepFreeze));
        env.close();

        auto const brokerSleBefore = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSleBefore))
            return;
        Number const coverAvailableBefore = brokerSleBefore->at(sfCoverAvailable);
        auto const brokerAccountId = brokerSleBefore->at(sfAccount);
        Number const brokerPseudoBalanceBefore =
            env.balance(Account{"brokerPseudo", brokerAccountId}, asset).value();

        auto const loanSleBefore = env.le(feeLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        BEAST_EXPECT(loanSleBefore->at(sfPaymentRemaining) == 1);

        env(pay(dustBorrower, feeLoanKeylet.key, asset(dustPrincipal + fee).value()));
        env.close();

        auto const loanSleAfter = env.le(feeLoanKeylet);
        auto const brokerSleAfter = env.le(brokerKeylet);
        if (!BEAST_EXPECT(loanSleAfter) || !BEAST_EXPECT(brokerSleAfter))
            return;
        BEAST_EXPECT(loanSleAfter->at(sfPaymentRemaining) == 0);
        BEAST_EXPECT(loanSleAfter->at(sfPrincipalOutstanding) == beast::kZero);

        Number const coverAvailableAfter = brokerSleAfter->at(sfCoverAvailable);
        Number const coverDelta = coverAvailableAfter - coverAvailableBefore;
        BEAST_EXPECT(coverDelta > beast::kZero);

        Number const brokerPseudoBalanceAfter =
            env.balance(Account{"brokerPseudo", brokerAccountId}, asset).value();
        BEAST_EXPECT(brokerPseudoBalanceAfter - brokerPseudoBalanceBefore == coverDelta);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultTerminalCloseFeeToCoverNonZero");
    }

    void
    testCoarsenedVaultCreditKeepsAvailableGridDigit()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: on a vault where AssetsAvailable's grid is finer than AssetsTotal's, a "
            "payment keeps the digit AA can hold");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // otherPrincipal is off-grid at 1e-10. With overdueDays=200 the vault's
        // getVaultScale coarsens to -9 while AssetsAvailable (~723,562) stays exactly
        // representable at the base grid -10.
        Number const otherPrincipal{1234'0000000007LL, -10};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{.principal = otherPrincipal}}, .overdueDays = 200});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& otherLoanKeylet = coarsened.loanKeylets[1];

        auto const vaultSleBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        BEAST_EXPECT(getVaultScale(vaultSleBefore) > getVaultBaseScale(vaultSleBefore));

        auto const before = snapshotVault(env, vaultKeylet, asset);

        auto const loanSleBefore = env.le(otherLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        Number const principalBefore = loanSleBefore->at(sfPrincipalOutstanding);

        env(pay(depositor, otherLoanKeylet.key, asset(otherPrincipal).value(), tfLoanLatePayment));
        env.close();

        auto const loanSleAfter = env.le(otherLoanKeylet);
        auto const vaultSleAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(loanSleAfter) || !BEAST_EXPECT(vaultSleAfter))
            return;
        Number const principalPaid =
            principalBefore - Number(loanSleAfter->at(sfPrincipalOutstanding));
        auto const after = snapshotVault(env, vaultKeylet, asset);

        // The full off-grid amount transfers because AssetsAvailable's grid is finer
        // than AssetsTotal's cache grid.
        BEAST_EXPECT(after.available - before.available == principalPaid);
        BEAST_EXPECT(
            principalPaid !=
            roundToAsset(asset, principalPaid, -9, Number::RoundingMode::Downward));
        BEAST_EXPECT(
            after.available - before.available == after.vaultBalance - before.vaultBalance);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultCreditKeepsAvailableGridDigit");
    }

    // Regression for a ValidVault false positive: the invariant rounded the
    // pseudo-account delta to AssetsTotal's coarser scale before the sign check,
    // so a sub-unit debit (under 5e-10) rounded to zero and failed with
    // tecINVARIANT_FAILED.
    void
    testCoarsenedVaultCreditCrossesPowerOfTen()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: a credit that crosses a power of ten keeps AssetsAvailable at 16 digits");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // crossingLoan is not yet due at the coarsening loan's ~344-day-overdue
        // mark, which leaves AssetsAvailable just under a power of ten. The
        // overpayment is computed from the observed AssetsAvailable so it crosses
        // that power of ten by construction.
        Number const crossingPrincipal{10'000};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = crossingPrincipal,
                 .flags = tfLoanOverpayment,
                 .paymentInterval = 1000 * 24 * 60 * 60}},
             .overdueDays = 345});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& crossingLoanKeylet = coarsened.loanKeylets[1];

        auto const before = snapshotVault(env, vaultKeylet, asset);
        BEAST_EXPECT(before.available <= before.total);

        // The next power of ten above the balance: crossing it adds a digit to the
        // integer part.
        Number const nextPowerOfTen = smallestPowerOfTenAbove(before.available);
        Number const crossingPayment = nextPowerOfTen - before.available + Number{1};

        auto const loanSleBefore = env.le(crossingLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        // The loan must hold enough principal for the crossing payment; if not,
        // grow crossingPrincipal.
        BEAST_EXPECT(crossingPayment <= Number(loanSleBefore->at(sfPrincipalOutstanding)));

        env(pay(
            depositor, crossingLoanKeylet.key, asset(crossingPayment).value(), tfLoanOverpayment));
        env.close();

        // Representable, within stored AssetsTotal, and past the boundary.
        Number const availableAfter = expectAvailableAtSixteenDigits(env, vaultKeylet, asset);
        BEAST_EXPECT(availableAfter >= nextPowerOfTen);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultCreditCrossesPowerOfTen");
    }

    // Regression for a ValidVault false positive: a single VaultWithdraw or
    // VaultClawback that takes the stored AssetsTotal cache across its own
    // floor-rounding grid boundary tripped the invariant, although AssetsAvailable
    // moved by exactly the transferred amount. Independent floor-rounding of the
    // before/after AssetsTotal snapshots can differ from the real delta by more
    // than the invariant's one-unit tolerance.
    // A final VaultWithdraw of all shares must succeed when AssetsDeployed is
    // zero, even if the shares-to-assets round-trip would round the payout above
    // AssetsAvailable.
    // Pay half of LoanBroker_test's former testFixedPrecisionCover minCoverBroker
    // scenario: an origination at the minimum-cover grid followed by a full
    // single payment, checked the same way LoanSet's origination half is.
    void
    testMinCoverBrokerLoanPayment()
    {
        using namespace test::jtx;
        using namespace loan_broker;

        testcase("FixedPrecision LoanBroker cover: minCoverBroker loan payment");

        FeatureBitset const v12{all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2};
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const borrower{"borrower"};
        Env env{*this, v12};
        env.fund(XRP(100'000), issuer, alice, borrower);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const iou = issuer["IOU"];
        env(trust(alice, iou(Number{10, 10})));
        env(trust(borrower, iou(Number{10, 10})));
        env(pay(issuer, alice, iou(Number{10, 9})));
        env(pay(issuer, borrower, iou(Number{10, 2})));

        test::jtx::Vault const vault{env};
        [[maybe_unused]] auto [createTx, vaultKeylet, subscriptionDate] =
            vault.createClosedEnded({.owner = alice, .asset = iou});
        createTx[sfScale] = 6;
        env(createTx);
        env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = iou(100)}));
        vault.closePastSubscription(subscriptionDate);

        auto const minCoverBroker =
            keylet::loanBroker(alice.id(), SeqProxy::rawSequence(env.seq(alice)));
        env(set(alice, vaultKeylet.key),
            kDebtMaximum(Number{100}),
            kCoverRateMinimum(percentageToTenthBips(10)),
            kCoverRateLiquidation(percentageToTenthBips(25)));
        env(coverDeposit(alice, minCoverBroker.key, iou(Number{1, -1})));
        env(loan::set(borrower, minCoverBroker.key, Number{1}),
            Sig(sfCounterpartySignature, alice),
            Fee(env.current()->fees().base * 2));
        {
            auto const broker = env.le(minCoverBroker);
            BEAST_EXPECT(broker);
            if (broker)
                BEAST_EXPECT((broker->at(sfDebtTotal) == Number{1}));
        }
        test::checkFixedPrecisionVaultAssetsDeployed(
            *this,
            env,
            vaultKeylet,
            {},
            {keylet::loan(minCoverBroker.key, SeqProxy::rawSequence(1))},
            "after minCoverBroker loan origination");

        auto const loanKeylet = keylet::loan(minCoverBroker.key, SeqProxy::rawSequence(1));
        env(loan::pay(borrower, loanKeylet.key, iou(1).value()));
        test::checkFixedPrecisionVaultAssetsDeployed(
            *this, env, vaultKeylet, {}, {loanKeylet}, "after minCoverBroker loan payment");
    }

    // Pay half of LoanSetFixedPrecision_test::testVaultDeleteRefusedWhileDebtOutstanding:
    // once the open loan is paid off in full and removed along with its
    // debt-free broker, VaultDelete succeeds after the depositor withdraws the
    // remaining shares.
    void
    testVaultDeleteAllowedAfterLoanPayoff()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: paying a FixedPrecision loan off in full allows VaultDelete once the "
            "loan and broker are removed");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{1'000});
        Number const principal{1'000};
        auto const loanKeylet = openLoan(env, fixture, principal, 1);

        test::jtx::Vault const vault{env};

        // Close the loan: one payment, zero interest.
        auto const loanSle0 = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle0))
            return;
        Number const periodicPayment = loanSle0->at(sfPeriodicPayment);
        std::int32_t const loanScale = loanSle0->at(sfLoanScale);
        Number const payoff = roundPeriodicPayment(asset, periodicPayment, loanScale);
        env(pay(depositor, loanKeylet.key, asset(payoff).value()));
        env.close();

        // LoanPay zeroes PrincipalOutstanding and PaymentRemaining on the final
        // payment but does not delete the Loan; that needs LoanDelete.
        auto const loanAfterPayoff = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanAfterPayoff))
            return;
        BEAST_EXPECT(Number(loanAfterPayoff->at(sfPrincipalOutstanding)) == beast::kZero);
        BEAST_EXPECT(loanAfterPayoff->at(sfPaymentRemaining) == 0);

        auto const brokerSle = env.le(fixture.brokerKeylet);
        if (BEAST_EXPECT(brokerSle))
            BEAST_EXPECT(Number(brokerSle->at(sfDebtTotal)) == beast::kZero);

        // The loan is closed but the vault still holds cash and the depositor its
        // shares, so VaultDelete is still refused.
        env(vault.del({.owner = owner, .id = fixture.vaultKeylet.key}), Ter(tecHAS_OBLIGATIONS));
        env.close();

        // Delete the closed Loan and the debt-free LoanBroker; they would otherwise
        // keep the pseudo-account non-empty.
        env(loan::del(depositor, loanKeylet.key));
        env.close();
        env(loan_broker::del(owner, fixture.brokerKeylet.key));
        env.close();

        // Past the Investment phase the depositor redeems every share, then delete
        // succeeds.
        env.close(fixture.redemptionDate + std::chrono::seconds{1});
        auto const vaultSleFull = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSleFull))
            return;
        Number const remaining = vaultSleFull->at(sfAssetsAvailable);
        env(fixture.vault.withdraw(
            {.depositor = depositor, .id = fixture.vaultKeylet.key, .amount = asset(remaining)}));
        env.close();

        auto const vaultSleEmpty = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSleEmpty))
            return;
        BEAST_EXPECT(Number(vaultSleEmpty->at(sfAssetsAvailable)) == beast::kZero);

        env(vault.del({.owner = owner, .id = fixture.vaultKeylet.key}), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!env.le(fixture.vaultKeylet));
    }

    // Rejects origination on a Vault that is already coarsened (its stored
    // scale differs from its base scale), independent of this loan's own
    // interest. This exercises the "already coarsened" guard specifically:
    // the loan requested here carries zero interest, so the Open-zone
    // capacity check would pass trivially, and only the coarsened-vault
    // check can be the reason for the rejection.
    void
    testOriginationRejectedOnAlreadyCoarsenedVault()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase("LoanSet rejects origination on an already-coarsened FixedPrecision vault");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.depositorTrust = 30'000'000, .depositorFunds = 10'000'000});

        auto const fixture = setupLendingVault(
            env,
            owner,
            depositor,
            asset,
            Number{899'999},
            std::chrono::seconds{473'040'000},
            std::uint8_t{10});

        // Originate one loan with the maximum late-interest rate, then let it
        // go overdue long enough that a late payment coarsens the Vault
        // (pushes getVaultScale() away from getVaultBaseScale()).
        std::uint32_t const gracePeriod = 60;
        auto const coarseningLoanKeylet =
            keylet::loan(fixture.brokerKeylet.key, SeqProxy::rawSequence(1));
        env(set(depositor, fixture.brokerKeylet.key, Number{700'000}),
            kInterestRate(TenthBips32(0)),
            kLateInterestRate(lending::kMaxLateInterestRate),
            kGracePeriod(gracePeriod),
            kPaymentInterval(24 * 60 * 60),
            kPaymentTotal(5),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(coarseningLoanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);

        env.close(
            NetClock::time_point{NetClock::duration{dueDate + gracePeriod}} +
            std::chrono::seconds{800 * 24 * 60 * 60});

        env(pay(depositor, coarseningLoanKeylet.key, asset(7'000'000).value(), tfLoanLatePayment));
        env.close();

        auto const vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        // The precondition for this test: the Vault is now coarsened.
        BEAST_EXPECT(getVaultScale(vaultSle) != getVaultBaseScale(vaultSle));

        // Zero interest: the Open-zone capacity check alone would pass. Only
        // the "already coarsened" check can produce the rejection.
        env(set(depositor, fixture.brokerKeylet.key, Number{1}),
            kInterestRate(TenthBips32(0)),
            kGracePeriod(gracePeriod),
            kPaymentInterval(24 * 60 * 60),
            kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2),
            Ter(tecLIMIT_EXCEEDED));
        env.close();

        checkVaultLoanSums(env, fixture, {coarseningLoanKeylet}, "already-coarsened rejection");
    }

    // The next tests try to originate a loan whose interest would coarsen a
    // FixedPrecision vault. The LoanSet Open-zone guard rejects it before that
    // happens, so each asserts the rejection.

    // A 100%-interest, single one-year-payment loan of principal would
    // recognize about its principal again as interest, which the Open-zone guard
    // rejects: the vault stays at its base scale with no debt.
public:
    void
    run() override
    {
        testLendingAssetsDeployedPayments();
        testLendingAssetsDeployedInterestPayoff();
        testLendingAssetsDeployedOverpayment();
        testLendingAssetsDeployedEarlyFullPayoff();
        testLendingAssetsDeployedIntegralAsset();
        testLateInterestCoarsensVault();
        testCoarsenedVaultCreditFlooring();
        testCoarseningPaymentCreditNeverExceedsPaid();
        testCoarsenedVaultSubUnitRemainderPayments();
        testCoarsenedVaultTerminalCloseWithFee();
        testCoarsenedVaultTerminalCloseFeeToCoverFloorsToZero();
        testCoarsenedVaultTerminalCloseFeeToCoverNonZero();
        testCoarsenedVaultCreditKeepsAvailableGridDigit();
        testCoarsenedVaultCreditCrossesPowerOfTen();
        testMinCoverBrokerLoanPayment();
        testVaultDeleteAllowedAfterLoanPayoff();
        testOriginationRejectedOnAlreadyCoarsenedVault();
    }
};

BEAST_DEFINE_TESTSUITE(LoanPayFixedPrecision, app, xrpl);

}  // namespace xrpl
