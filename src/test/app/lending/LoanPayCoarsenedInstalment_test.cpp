#include <test/app/lending/LoanFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <ostream>
#include <string>
#include <tuple>

namespace xrpl {

// On a coarsened vault a small loan's instalment rounds to no credit. LoanPay rejects a payment
// whose credit rounds to zero, unless it is the payment that closes the loan. Only that final
// payment may forgive the remainder. This is by design.
//
// AA = AssetsAvailable, AD = AssetsDeployed, PO = PrincipalOutstanding.
//
// 1. A closed-ended IOU vault at Scale 1 (base unit 0.1) takes a deposit of 899,999,000,000,000.
// 2. Loan A lends 700,000,000,000,000 at zero interest with the maximum LateInterestRate. Loan B
//    lends 0.5 to a separate borrower in five installments of 0.1, 1,000 days apart.
// 3. Loan A's first payment is paid 800 days late. The late interest takes AA to about
//    1.87 * 10^15, past 10^15 (10^16 base units), so AA is stored on a grid of 1.
// 4. Before its first due date the borrower pays loan B's instalment of 0.1. AA + 0.1 floors back
//    to AA, so the vault would be credited 0 and LoanPay rejects the payment with
//    tecPRECISION_LOSS. Two installments (0.2) fail the same way.
// 5. Paying all five installments at once (0.5), or a full payment, closes loan B. The credit is
//    still 0, but LoanPay lets the payment that closes a loan through, so the borrower pays
//    nothing and AD drops by PO = 0.5.
// 6. After the due date only a late payment is allowed, and it covers one instalment. The late
//    0.1 fails like step 4, and a regular or full payment returns tecEXPIRED.
//
// With no LateInterestRate, a late loan B can't be repaid, and only a default remains.
//
// With a LateInterestRate, a late payment of one instalment can become representable. Loan B at
// LateInterestRate 100%, paid two years late, owes 0.1 + 0.5 * 2 = 1.1. The vault is credited 1
// and the 0.1 remainder is forgiven. Because the instalment is below one vault unit, the
// remainder is always less than the late interest, so the vault gets all of the principal and
// only part of the late interest is forgiven.
class LoanPayCoarsenedInstalment_test : public LoanFixedPrecisionBase
{
    static constexpr std::uint8_t kScale = 1;
    static constexpr std::uint32_t kLoanGracePeriod = 60;
    static constexpr std::uint32_t kOneDay = 24 * 60 * 60;
    static constexpr std::uint32_t kLoanBInstallments = 5;
    // Loan B's first due date comes after loan A's 800-day late payment.
    static constexpr std::uint32_t kLoanBInterval = 1'000 * kOneDay;

    struct Setup
    {
        AssetAccounts accounts;
        LendingFixture fixture;
        test::jtx::Account borrower;
        Keylet loanB;
    };

    // Loan B's instalment and one base unit.
    static Number
    instalment()
    {
        return Number{1, -1};
    }

    static TER
    submitAndClose(test::jtx::Env& env, json::Value const& tx)
    {
        env(tx, test::jtx::Ter(std::ignore));
        TER const ter = env.ter();
        env.close();
        return ter;
    }

    // A zero-interest loan from the fixture's broker, countersigned by the broker owner.
    static Keylet
    createLoan(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        test::jtx::Account const& borrower,
        Number const& principal,
        std::uint32_t paymentTotal,
        std::uint32_t paymentInterval,
        TenthBips32 lateInterestRate)
    {
        using namespace test::jtx;
        using namespace loan;

        auto const brokerSle = env.le(fixture.brokerKeylet);
        auto const loanKeylet = keylet::loan(
            fixture.brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        env(set(borrower, fixture.brokerKeylet.key, principal),
            kInterestRate(TenthBips32(0)),
            kLateInterestRate(lateInterestRate),
            kGracePeriod(kLoanGracePeriod),
            kPaymentInterval(paymentInterval),
            kPaymentTotal(paymentTotal),
            Sig(sfCounterpartySignature, fixture.owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        return loanKeylet;
    }

    NetClock::time_point
    nextDueDate(test::jtx::Env& env, Keylet const& loanKeylet)
    {
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return env.now();
        return NetClock::time_point{NetClock::duration{loanSle->at(sfNextPaymentDueDate)}};
    }

    // Steps 1 to 3.
    Setup
    setupCoarsenedVault(test::jtx::Env& env, TenthBips32 loanBLateInterestRate = TenthBips32(0))
    {
        using namespace test::jtx;
        using namespace std::chrono_literals;

        auto const accounts = setupIou(
            env,
            {
                .depositorTrust = Number{30'000'000'000'000'000LL},
                .depositorFunds = Number{10'000'000'000'000'000LL},
                .ownerFunds = Number{100},
            });
        auto const& asset = accounts.asset;
        Account const borrower{"borrower"};
        fundIouHolder(env, borrower, accounts, Number{1'000}, Number{1});

        // 1. The vault and its broker. The Investment phase outlasts loan B.
        auto const fixture = setupLendingVault(
            env,
            accounts.owner,
            accounts.depositor,
            asset,
            Number{899'999'000'000'000LL},
            std::chrono::seconds{(kLoanBInstallments + 1) * std::uint64_t{kLoanBInterval}},
            kScale);

        // 2. Loans A and B.
        Keylet const loanA = createLoan(
            env,
            fixture,
            accounts.depositor,
            Number{700'000'000'000'000LL},
            5,
            kOneDay,
            lending::kMaxLateInterestRate);
        Keylet const loanB = createLoan(
            env,
            fixture,
            borrower,
            instalment() * kLoanBInstallments,
            kLoanBInstallments,
            kLoanBInterval,
            loanBLateInterestRate);

        // 3. Loan A's first payment, 800 days late, plus its late interest.
        env.close(nextDueDate(env, loanA) + NetClock::duration{kLoanGracePeriod} + 800 * 24h);
        TER const ter = submitAndClose(
            env,
            loan::pay(
                accounts.depositor,
                loanA.key,
                asset(Number{7'000'000'000'000'000LL}).value(),
                tfLoanLatePayment));
        BEAST_EXPECT(ter == tesSUCCESS);
        expectCoarsened(env, fixture.vaultKeylet);

        return {.accounts = accounts, .fixture = fixture, .borrower = borrower, .loanB = loanB};
    }

    static char const*
    paymentKind(std::uint32_t flags)
    {
        if (flags == tfLoanLatePayment)
            return " late";
        if (flags == tfLoanFullPayment)
            return " in full";
        return "";
    }

    // Pays amount on loan B with the given LoanPay flags and logs the result.
    TER
    payLoanB(test::jtx::Env& env, Setup const& setup, Number const& amount, std::uint32_t flags)
    {
        auto const& asset = setup.accounts.asset;
        TER const ter = submitAndClose(
            env,
            test::jtx::loan::pay(setup.borrower, setup.loanB.key, asset(amount).value(), flags));
        auto const loanSle = env.le(setup.loanB);
        auto const vaultSle = env.le(setup.fixture.vaultKeylet);
        if (!BEAST_EXPECT(loanSle) || !BEAST_EXPECT(vaultSle))
            return ter;
        log << "paying " << amount << paymentKind(flags) << " returned " << transToken(ter)
            << ": PaymentRemaining " << loanSle->at(sfPaymentRemaining) << ", PO "
            << Number{loanSle->at(sfPrincipalOutstanding)} << ", AA "
            << Number{vaultSle->at(sfAssetsAvailable)} << ", AD "
            << Number{vaultSle->at(sfAssetsDeployed)} << ", borrower "
            << Number{env.balance(setup.borrower, asset).value()} << std::endl;
        return ter;
    }

    void
    testInstalmentOnTime()
    {
        testcase("A small loan's instalment paid on time on a coarsened vault");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        Setup const setup = setupCoarsenedVault(env);
        auto const before = snapshotVault(env, setup.fixture.vaultKeylet, setup.accounts.asset);
        Number const borrowerBefore = env.balance(setup.borrower, setup.accounts.asset).value();

        // 4. One and two installments, before the due date.
        // The credit rounds to zero and this is not the final payment, so it is rejected.
        TER const instalmentTer = payLoanB(env, setup, instalment(), 0);
        BEAST_EXPECTS(
            instalmentTer == tecPRECISION_LOSS,
            "paying the instalment of 0.1 on time returned " + transToken(instalmentTer));

        TER const twoInstallmentsTer = payLoanB(env, setup, instalment() * 2, 0);
        BEAST_EXPECT(twoInstallmentsTer == tecPRECISION_LOSS);

        // 5. All five installments close the loan with no cash. The vault writes off 0.5.
        TER const payoffTer = payLoanB(env, setup, instalment() * kLoanBInstallments, 0);
        BEAST_EXPECT(payoffTer == tesSUCCESS);
        auto const loanAfter = env.le(setup.loanB);
        BEAST_EXPECT(loanAfter && loanAfter->at(sfPaymentRemaining) == 0);
        auto const after = snapshotVault(env, setup.fixture.vaultKeylet, setup.accounts.asset);
        BEAST_EXPECT(after.available == before.available);
        BEAST_EXPECT(
            before.assetsDeployed - after.assetsDeployed == instalment() * kLoanBInstallments);
        BEAST_EXPECT(env.balance(setup.borrower, setup.accounts.asset).value() == borrowerBefore);
    }

    void
    testFullPaymentOnTime()
    {
        testcase("A full payment of a small loan on a coarsened vault");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        Setup const setup = setupCoarsenedVault(env);
        auto const before = snapshotVault(env, setup.fixture.vaultKeylet, setup.accounts.asset);
        Number const borrowerBefore = env.balance(setup.borrower, setup.accounts.asset).value();

        // 5. A full payment before the due date also closes the loan with no cash.
        TER const fullTer =
            payLoanB(env, setup, instalment() * kLoanBInstallments, tfLoanFullPayment);
        BEAST_EXPECT(fullTer == tesSUCCESS);
        auto const loanAfter = env.le(setup.loanB);
        BEAST_EXPECT(loanAfter && loanAfter->at(sfPaymentRemaining) == 0);
        auto const after = snapshotVault(env, setup.fixture.vaultKeylet, setup.accounts.asset);
        BEAST_EXPECT(after.available == before.available);
        BEAST_EXPECT(env.balance(setup.borrower, setup.accounts.asset).value() == borrowerBefore);
    }

    void
    testInstalmentLate()
    {
        testcase("A small loan's late instalment on a coarsened vault");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        Setup const setup = setupCoarsenedVault(env);

        // 6. Every way to pay after the due date.
        env.close(nextDueDate(env, setup.loanB) + NetClock::duration{1});

        // There is no late interest, so the late payment is still 0.1 and is rejected.
        TER const lateTer = payLoanB(env, setup, instalment(), tfLoanLatePayment);
        BEAST_EXPECTS(
            lateTer == tecPRECISION_LOSS,
            "paying the instalment of 0.1 late returned " + transToken(lateTer));

        // A late payment covers one instalment, however much is sent.
        BEAST_EXPECT(
            payLoanB(env, setup, instalment() * kLoanBInstallments, tfLoanLatePayment) ==
            tecPRECISION_LOSS);
        BEAST_EXPECT(payLoanB(env, setup, instalment() * kLoanBInstallments, 0) == tecEXPIRED);
        BEAST_EXPECT(
            payLoanB(env, setup, instalment() * kLoanBInstallments, tfLoanFullPayment) ==
            tecEXPIRED);

        auto const loanAfter = env.le(setup.loanB);
        BEAST_EXPECT(loanAfter && loanAfter->at(sfPaymentRemaining) == kLoanBInstallments);
    }

    void
    testInstalmentLateWithLateInterest()
    {
        testcase("A small loan's late instalment becomes representable with late interest");

        using namespace test::jtx;
        using namespace std::chrono_literals;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        Setup const setup = setupCoarsenedVault(env, lending::kMaxLateInterestRate);
        auto const& asset = setup.accounts.asset;

        // Two years late, the late interest is 0.5 * 100% * 2 = 1, so the payment is 1.1.
        env.close(nextDueDate(env, setup.loanB) + 2 * 365 * 24h);
        auto const before = snapshotVault(env, setup.fixture.vaultKeylet, asset);
        Number const borrowerBefore = env.balance(setup.borrower, asset).value();

        TER const lateTer = payLoanB(env, setup, Number{15, -1}, tfLoanLatePayment);
        BEAST_EXPECTS(
            lateTer == tesSUCCESS,
            "paying the instalment late with late interest returned " + transToken(lateTer));

        auto const loanAfter = env.le(setup.loanB);
        BEAST_EXPECT(loanAfter && loanAfter->at(sfPaymentRemaining) == kLoanBInstallments - 1);
        auto const after = snapshotVault(env, setup.fixture.vaultKeylet, asset);
        Number const credit = after.available - before.available;
        Number const borrowerPaid = borrowerBefore - env.balance(setup.borrower, asset).value();
        // The exact AA + AD. The stored AssetsTotal is floored to 16 digits, so it moves by 1 here.
        Number const exactTotalChange =
            (after.available + after.assetsDeployed) - (before.available + before.assetsDeployed);
        log << "late payment with late interest: credit " << credit << ", borrower paid "
            << borrowerPaid << ", AD dropped by " << before.assetsDeployed - after.assetsDeployed
            << ", AA + AD changed by " << exactTotalChange << std::endl;

        // The vault is credited one whole unit, and the borrower pays only that.
        BEAST_EXPECT(credit == Number{1});
        BEAST_EXPECT(borrowerPaid == credit);
        // The whole instalment of principal is repaid.
        BEAST_EXPECT(before.assetsDeployed - after.assetsDeployed == instalment());
        // The 0.1 remainder came out of the late interest: the vault still gains 0.9.
        BEAST_EXPECT(exactTotalChange == credit - instalment());
    }

public:
    void
    run() override
    {
        testInstalmentOnTime();
        testFullPaymentOnTime();
        testInstalmentLate();
        testInstalmentLateWithLateInterest();
    }
};

BEAST_DEFINE_TESTSUITE(LoanPayCoarsenedInstalment, app, xrpl);

}  // namespace xrpl
