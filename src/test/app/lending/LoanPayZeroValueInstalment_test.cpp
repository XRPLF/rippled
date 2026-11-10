#include <test/app/lending/LoanFixedPrecisionBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
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

// Every instalment of a loan whose PeriodicPayment is below one unit is worth one unit, so a late
// payment always moves the loan.
//
// Without LendingProtocolV1_2 an instalment could be worth 0, and a late payment of it failed
// LoanInvariant, so the loan could only be defaulted:
//
// AA = AssetsAvailable, TVO = TotalValueOutstanding, PO = PrincipalOutstanding,
// MFO = ManagementFeeOutstanding.
//
// 1. An IOU vault at Scale 0 holds AA = 10^6. Its broker has ManagementFeeRate = 10%.
// 2. Loan A lends PO = 6 at InterestRate = 14.5% and LateInterestRate = 100% in 16 yearly
//    installments. At LoanScale 0 TVO = 16 and MFO = 1.
// 3. Installments 1 to 3 are paid early, 1 of interest each.
// 4. Instalment 4 is paid 0.2 years late: 1 of interest + 1 of late interest, as
//    6 * 100% * 0.2 = 1.2 rounds to 1. Now TVO = 12, PO = 6 and MFO = 1.
// 5. The rounded schedule after instalment 5 is also TVO = 12, PO = 6, MFO = 1, so instalment 5 is
//    worth 0. Paid 0.2 years late it is only 1 of late interest. Then TVO' = TVO and PO' = PO, and
//    LoanInvariant fails with "loan pay must decrease PrincipalOutstanding or
//    TotalValueOutstanding on a non-full-repayment".
//
// With LendingProtocolV1_2 Guard 4 makes TVO exactly 16 units for a rounded PeriodicPayment of 1,
// and each instalment brings TVO down by one unit, so instalment 5 is worth 1.
class LoanPayZeroValueInstalment_test : public LoanFixedPrecisionBase
{
    static constexpr std::uint8_t kScale = 0;
    static constexpr std::uint32_t kPaymentTotal = 16;
    static constexpr std::uint32_t kPaymentInterval = 365 * 24 * 60 * 60;
    static constexpr std::uint32_t kGracePeriod = 60;
    // 0.2 years.
    static constexpr NetClock::duration kSecondsLate{kPaymentInterval / 5};

    static TER
    submitAndClose(test::jtx::Env& env, json::Value const& tx)
    {
        env(tx, test::jtx::Ter(std::ignore));
        TER const result = env.ter();
        env.close();
        return result;
    }

    // Loan A, with a LateInterestRate, which openLoan does not set.
    static Keylet
    openLoanWithLateInterest(test::jtx::Env& env, LendingFixture const& fixture)
    {
        using namespace test::jtx;

        auto const brokerSle = env.le(fixture.brokerKeylet);
        Keylet const loanKeylet = keylet::loan(
            fixture.brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        env(loan::set(fixture.depositor, fixture.brokerKeylet.key, Number{6}, 0),
            loan::kInterestRate(TenthBips32(14'500)),
            loan::kLateInterestRate(TenthBips32(100'000)),
            loan::kGracePeriod(kGracePeriod),
            loan::kPaymentInterval(kPaymentInterval),
            loan::kPaymentTotal(kPaymentTotal),
            Sig(sfCounterpartySignature, fixture.owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        return loanKeylet;
    }

    static TER
    payLoan(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        test::jtx::PrettyAsset const& asset,
        Keylet const& loanKeylet,
        Number const& amount,
        std::uint32_t flags = 0)
    {
        return submitAndClose(
            env,
            test::jtx::loan::pay(fixture.depositor, loanKeylet.key, asset(amount).value(), flags));
    }

    // Closes the ledger 0.2 years after the next due date and pays amount late.
    TER
    payLate(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        test::jtx::PrettyAsset const& asset,
        Keylet const& loanKeylet,
        Number const& amount)
    {
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return tefINTERNAL;
        NetClock::time_point const dueDate{NetClock::duration{loanSle->at(sfNextPaymentDueDate)}};
        env.close(dueDate + kSecondsLate);
        return payLoan(env, fixture, asset, loanKeylet, amount, tfLoanLatePayment);
    }

    void
    logLoan(
        test::jtx::Env& env,
        Keylet const& loanKeylet,
        Keylet const& vaultKeylet,
        std::string const& label)
    {
        auto const loanSle = env.le(loanKeylet);
        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(loanSle) || !BEAST_EXPECT(vaultSle))
            return;
        log << label << ": PaymentRemaining " << loanSle->at(sfPaymentRemaining) << ", TVO "
            << Number{loanSle->at(sfTotalValueOutstanding)} << ", PO "
            << Number{loanSle->at(sfPrincipalOutstanding)} << ", MFO "
            << Number{loanSle->at(sfManagementFeeOutstanding)} << ", AA "
            << Number{vaultSle->at(sfAssetsAvailable)} << std::endl;
    }

    void
    testZeroValueInstalment()
    {
        testcase(
            "Every instalment of a loan with PeriodicPayment below one unit is worth one unit");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        auto const accounts = setupIou(
            env,
            {
                .depositorTrust = Number{10'000'000},
                .depositorFunds = Number{2'000'000},
                .ownerFunds = Number{100},
            });
        auto const& asset = accounts.asset;

        // 1. The vault and its broker. The Investment phase outlasts the loan.
        auto const fixture = setupLendingVault(
            env,
            accounts.owner,
            accounts.depositor,
            asset,
            Number{1'000'000},
            std::chrono::seconds{(kPaymentTotal + 2) * std::uint64_t{kPaymentInterval}},
            kScale,
            TenthBips32(0),
            TenthBips32(0),
            TenthBips16(10'000));

        // 2. Loan A.
        Keylet const loanA = openLoanWithLateInterest(env, fixture);
        logLoan(env, loanA, fixture.vaultKeylet, "loan A created");

        auto const valueOutstanding = [&] -> Number {
            auto const loanSle = env.le(loanA);
            if (!BEAST_EXPECT(loanSle))
                return Number{};
            return loanSle->at(sfTotalValueOutstanding);
        };
        BEAST_EXPECT(valueOutstanding() == Number{kPaymentTotal});

        // Each instalment, paid early or late, takes exactly one unit off TVO. A late one also
        // pays one unit of late interest.
        auto const payInstalment = [&](std::uint32_t instalment, bool late) {
            Number const before = valueOutstanding();
            TER const result = late ? payLate(env, fixture, asset, loanA, Number{2})
                                    : payLoan(env, fixture, asset, loanA, Number{1});
            logLoan(
                env, loanA, fixture.vaultKeylet, "after instalment " + std::to_string(instalment));
            BEAST_EXPECTS(
                result == tesSUCCESS,
                "instalment " + std::to_string(instalment) + " returned " + transToken(result));
            BEAST_EXPECT(before - valueOutstanding() == Number{1});
        };

        // 3. Installments 1 to 3, paid early.
        for (std::uint32_t instalment = 1; instalment <= 3; ++instalment)
            payInstalment(instalment, false);

        // 4 and 5. Installments 4 and 5, paid late.
        payInstalment(4, true);
        payInstalment(5, true);

        // The rest, paid early.
        for (std::uint32_t instalment = 6; instalment <= kPaymentTotal; ++instalment)
            payInstalment(instalment, false);

        auto const loanSle = env.le(loanA);
        if (!BEAST_EXPECT(loanSle))
            return;
        BEAST_EXPECT(loanSle->at(sfPaymentRemaining) == 0);
        BEAST_EXPECT(loanSle->at(sfTotalValueOutstanding) == beast::kZero);
    }

public:
    void
    run() override
    {
        testZeroValueInstalment();
    }
};

BEAST_DEFINE_TESTSUITE(LoanPayZeroValueInstalment, app, xrpl);

}  // namespace xrpl
