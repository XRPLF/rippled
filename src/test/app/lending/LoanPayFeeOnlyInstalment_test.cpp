#include <test/app/lending/LoanFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
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
#include <optional>
#include <ostream>
#include <string>
#include <tuple>

namespace xrpl {

// An instalment that is all management fee. Its vault part is exactly 0, so nothing is owed to the
// vault. LoanPay used to reject it, so the loan could only be defaulted.
//
// AA = AssetsAvailable, AD = AssetsDeployed, YU = YieldUnrealized, TVO = TotalValueOutstanding,
// PO = PrincipalOutstanding, MFO = ManagementFeeOutstanding.
//
// 1. A vault at Scale 0 holds AA = 10^12. Its broker has ManagementFeeRate = 10%.
// 2. Loan A lends PO = 2 at InterestRate = 45.5% in 12 yearly installments. At LoanScale 0
//    TVO = 12 and MFO = 1.
// 3. With LendingProtocolV1_2 the first instalment of 1 is the management fee and nothing else.
//    It all goes to the broker, so AA, AD and YU don't change, TVO and PaymentRemaining drop by
//    one, and the vault is not forgiven anything. A payment whose vault part is positive but
//    rounds to 0 is still rejected (LoanPayCoarsenedInstalment).
// 4. The same instalment paid late with LateInterestRate 0, and with an MPT, behaves the same.
// 5. The fee goes into CoverAvailable when the broker owner cannot receive it, and nothing moves
//    when the borrower is the broker owner, who would pay the fee to itself.
class LoanPayFeeOnlyInstalment_test : public LoanFixedPrecisionBase
{
    static constexpr std::uint8_t kScale = 0;
    static constexpr std::uint32_t kPaymentTotal = 12;
    static constexpr std::uint32_t kPaymentInterval = 365 * 24 * 60 * 60;
    static constexpr std::uint32_t kGracePeriod = 60;
    static constexpr std::uint32_t kInstallments = 9;
    // More than the close time resolution.
    static constexpr NetClock::duration kSecondsBeforeDue{100};

    // Where the management fee of a fee-only instalment goes.
    enum class FeeTo { Owner, Cover, Self };

    // The values a fee-only instalment must leave alone or move by exactly one unit.
    struct Snapshot
    {
        Number totalValue{0};
        std::uint32_t paymentRemaining{0};
        Number managementFee{0};
        Number available{0};
        Number deployed{0};
        Number yield{0};
        Number debtTotal{0};
        Number owner{0};
        Number cover{0};
    };

    static TER
    submitAndClose(test::jtx::Env& env, json::Value const& tx)
    {
        env(tx, test::jtx::Ter(std::ignore));
        TER const result = env.ter();
        env.close();
        return result;
    }

    // Pays 1, the amount of every instalment.
    static TER
    payInstalment(
        test::jtx::Env& env,
        test::jtx::Account const& payer,
        test::jtx::PrettyAsset const& asset,
        Keylet const& loanKeylet,
        std::uint32_t flags = 0)
    {
        return submitAndClose(
            env, test::jtx::loan::pay(payer, loanKeylet.key, asset(Number{1}).value(), flags));
    }

    NetClock::time_point
    nextDueDate(test::jtx::Env& env, Keylet const& loanKeylet)
    {
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return env.now();
        return NetClock::time_point{NetClock::duration{loanSle->at(sfNextPaymentDueDate)}};
    }

    Snapshot
    snapshot(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        test::jtx::PrettyAsset const& asset,
        Keylet const& loanKeylet)
    {
        auto const loanSle = env.le(loanKeylet);
        auto const vaultSle = env.le(fixture.vaultKeylet);
        auto const brokerSle = env.le(fixture.brokerKeylet);
        if (!BEAST_EXPECT(loanSle) || !BEAST_EXPECT(vaultSle) || !BEAST_EXPECT(brokerSle))
            return {};
        return {
            .totalValue = loanSle->at(sfTotalValueOutstanding),
            .paymentRemaining = loanSle->at(sfPaymentRemaining),
            .managementFee = loanSle->at(sfManagementFeeOutstanding),
            .available = vaultSle->at(sfAssetsAvailable),
            .deployed = vaultSle->at(sfAssetsDeployed),
            .yield = vaultSle->at(sfYieldUnrealized),
            .debtTotal = brokerSle->at(sfDebtTotal),
            .owner = Number{env.balance(fixture.owner, asset).value()},
            .cover = brokerSle->at(sfCoverAvailable),
        };
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

    // Pays one instalment of 1 and checks it. Every instalment takes one unit off TVO and
    // PaymentRemaining. A fee-only instalment pays the fee to feeTo and leaves the vault side and
    // DebtTotal alone. Any other instalment here is all principal and interest: the vault gets 1
    // and the broker nothing.
    void
    payAndCheck(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        test::jtx::PrettyAsset const& asset,
        test::jtx::Account const& payer,
        Keylet const& loanKeylet,
        std::uint32_t flags,
        bool feeOnly,
        std::string const& label,
        FeeTo feeTo = FeeTo::Owner)
    {
        auto const before = snapshot(env, fixture, asset, loanKeylet);
        TER const result = payInstalment(env, payer, asset, loanKeylet, flags);
        logLoan(env, loanKeylet, fixture.vaultKeylet, label + " returned " + transToken(result));
        if (!BEAST_EXPECTS(result == tesSUCCESS, label + " returned " + transToken(result)))
            return;

        auto const after = snapshot(env, fixture, asset, loanKeylet);
        BEAST_EXPECTS(before.totalValue - after.totalValue == 1, label + " TVO");
        BEAST_EXPECTS(before.paymentRemaining - after.paymentRemaining == 1, label + " remaining");
        if (feeOnly)
        {
            BEAST_EXPECTS(after.available == before.available, label + " AA");
            BEAST_EXPECTS(after.deployed == before.deployed, label + " AD");
            BEAST_EXPECTS(after.yield == before.yield, label + " YU");
            BEAST_EXPECTS(after.debtTotal == before.debtTotal, label + " DebtTotal");
            BEAST_EXPECTS(before.managementFee - after.managementFee == 1, label + " MFO");
            BEAST_EXPECTS(
                after.owner - before.owner == (feeTo == FeeTo::Owner ? 1 : 0), label + " owner");
            BEAST_EXPECTS(
                after.cover - before.cover == (feeTo == FeeTo::Cover ? 1 : 0), label + " cover");
        }
        else
        {
            BEAST_EXPECTS(after.available - before.available == 1, label + " AA");
            BEAST_EXPECTS(after.managementFee == before.managementFee, label + " MFO");
            BEAST_EXPECTS(after.owner == before.owner, label + " owner");
            BEAST_EXPECTS(after.cover == before.cover, label + " cover");
        }
    }

    static LendingFixture
    setupFixture(
        test::jtx::Env& env,
        AssetAccounts const& accounts,
        std::optional<std::uint8_t> scale = kScale)
    {
        return setupLendingVault(
            env,
            accounts.owner,
            accounts.depositor,
            accounts.asset,
            Number{1'000'000'000'000LL},
            std::chrono::seconds{(kPaymentTotal + 2) * std::uint64_t{kPaymentInterval}},
            scale,
            TenthBips32(0),
            TenthBips32(0),
            TenthBips16(10'000));
    }

    static Keylet
    openLoanA(test::jtx::Env& env, LendingFixture const& fixture)
    {
        return openLoan(
            env,
            fixture,
            Number{2},
            kPaymentTotal,
            TenthBips32(45'500),
            0,
            kPaymentInterval,
            kGracePeriod);
    }

    static AssetAccounts
    setupIouAccounts(test::jtx::Env& env)
    {
        return setupIou(
            env,
            {
                .depositorTrust = Number{10'000'000'000'000LL},
                .depositorFunds = Number{2'000'000'000'000LL},
                .ownerFunds = Number{100},
            });
    }

    static AssetAccounts
    setupMptAccounts(test::jtx::Env& env)
    {
        return setupMpt(
            env,
            {
                .maxAmt = 1'000'000'000'000'000ULL,
                .withDepositor = true,
                .holderFunds = 2'000'000'000'000ULL,
            });
    }

    void
    testFeeOnlyInstalment()
    {
        testcase("An instalment that is all management fee can be paid");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        auto const accounts = setupIouAccounts(env);
        auto const& asset = accounts.asset;

        // 1. The vault and its broker. The Investment phase outlasts the loan.
        auto const fixture = setupFixture(env, accounts);

        // 2. Loan A.
        Keylet const loanA = openLoanA(env, fixture);
        logLoan(env, loanA, fixture.vaultKeylet, "loan A created");

        // 3. Installments 1 to 9, each paid before its due date. The first is all fee.
        for (std::uint32_t instalment = 1; instalment <= kInstallments; ++instalment)
        {
            env.close(nextDueDate(env, loanA) - kSecondsBeforeDue);
            payAndCheck(
                env,
                fixture,
                asset,
                fixture.depositor,
                loanA,
                0,
                instalment == 1,
                "instalment " + std::to_string(instalment));
        }

        // 4. The late payment of the next instalment pays 1 of interest as usual.
        env.close(nextDueDate(env, loanA) + NetClock::duration{1});
        payAndCheck(
            env,
            fixture,
            asset,
            fixture.depositor,
            loanA,
            tfLoanLatePayment,
            false,
            "the late payment");
    }

    // The all-fee instalment paid late. With LateInterestRate 0 the late payment owes no late
    // interest, so the vault part is still 0. (With late interest it is no longer 0.)
    void
    testFeeOnlyInstalmentPaidLate()
    {
        testcase("An instalment that is all management fee can be paid late");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        auto const accounts = setupIouAccounts(env);
        auto const fixture = setupFixture(env, accounts);
        Keylet const loanA = openLoanA(env, fixture);

        env.close(nextDueDate(env, loanA) + NetClock::duration{1});
        payAndCheck(
            env,
            fixture,
            accounts.asset,
            fixture.depositor,
            loanA,
            tfLoanLatePayment,
            true,
            "late instalment 1");
    }

    void
    testFeeOnlyInstalmentMpt()
    {
        testcase("An instalment that is all management fee can be paid with an MPT");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        auto const accounts = setupMptAccounts(env);
        // An MPT vault takes no Scale.
        auto const fixture = setupFixture(env, accounts, std::nullopt);
        Keylet const loanA = openLoanA(env, fixture);

        env.close(nextDueDate(env, loanA) - kSecondsBeforeDue);
        payAndCheck(
            env, fixture, accounts.asset, fixture.depositor, loanA, 0, true, "MPT instalment 1");
    }

    // The broker owner is deep frozen, so it cannot receive the fee: it goes into
    // CoverAvailable instead.
    void
    testFeeOnlyInstalmentIntoCover()
    {
        testcase("An instalment that is all management fee can be paid into cover");

        using namespace test::jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        auto const accounts = setupIouAccounts(env);
        auto const fixture = setupFixture(env, accounts);
        Keylet const loanA = openLoanA(env, fixture);

        env(trust(
            accounts.issuer, accounts.asset(0), accounts.owner, tfSetFreeze | tfSetDeepFreeze));
        env.close();

        env.close(nextDueDate(env, loanA) - kSecondsBeforeDue);
        payAndCheck(
            env,
            fixture,
            accounts.asset,
            fixture.depositor,
            loanA,
            0,
            true,
            "instalment 1 into cover",
            FeeTo::Cover);
    }

    // The broker owner borrows from its own broker. The fee would go from the owner to itself, so
    // nothing moves, but the instalment is still paid.
    void
    testFeeOnlyInstalmentOwnerBorrower()
    {
        testcase("An instalment that is all management fee can be paid by the broker owner");

        using namespace test::jtx;
        using namespace loan;
        Env env{*this, all_ | featureLendingProtocolV1_1};
        auto const accounts = setupIouAccounts(env);
        auto const fixture = setupFixture(env, accounts);

        auto const brokerSle = env.le(fixture.brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return;
        Keylet const loanKeylet = keylet::loan(
            fixture.brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        env(set(fixture.owner, fixture.brokerKeylet.key, Number{2}),
            kInterestRate(TenthBips32(45'500)),
            loan::kGracePeriod(kGracePeriod),
            loan::kPaymentInterval(kPaymentInterval),
            loan::kPaymentTotal(kPaymentTotal),
            Sig(sfCounterpartySignature, fixture.owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        if (!BEAST_EXPECT(env.le(loanKeylet)))
            return;

        env.close(nextDueDate(env, loanKeylet) - kSecondsBeforeDue);
        payAndCheck(
            env,
            fixture,
            accounts.asset,
            fixture.owner,
            loanKeylet,
            0,
            true,
            "owner instalment 1",
            FeeTo::Self);
    }

public:
    void
    run() override
    {
        testFeeOnlyInstalment();
        testFeeOnlyInstalmentPaidLate();
        testFeeOnlyInstalmentMpt();
        testFeeOnlyInstalmentIntoCover();
        testFeeOnlyInstalmentOwnerBorrower();
    }
};

BEAST_DEFINE_TESTSUITE(LoanPayFeeOnlyInstalment, app, xrpl);

}  // namespace xrpl
