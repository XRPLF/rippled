#pragma once

#include <test/app/lending/LoanTestBase.h>
#include <test/app/vault/VaultFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

// Shared scaffolding for the lending FixedPrecision test suites
// (LoanSetFixedPrecision, and, via LoanPayFixedPrecisionBase /
// LoanManageFixedPrecisionBase, LoanPayFixedPrecision and
// LoanManageFixedPrecision): closed-ended lending vault and loan fixtures.
// Builds on the vault-only VaultFixedPrecisionBase instead of duplicating its
// account setup and vault assertions. Keep this header to what LoanSet-level
// tests need; the coarsened-vault fixture and the default/impair helpers live
// in the derived bases below so a LoanSet-only test file does not carry
// LoanPay/LoanManage-only scaffolding.
class LoanFixedPrecisionBase : public VaultFixedPrecisionBase
{
protected:
    // Funds holder and gives it a USD trust line with the given balance.
    static void
    fundIouHolder(
        test::jtx::Env& env,
        test::jtx::Account const& holder,
        AssetAccounts const& accounts,
        Number const& trustLimit,
        Number const& funds)
    {
        using namespace test::jtx;

        env.fund(XRP(1'000'000), holder);
        env.close();
        env(trust(holder, accounts.asset(trustLimit)));
        env.close();
        env(pay(accounts.issuer, holder, accounts.asset(funds)));
        env.close();
    }

    // A closed-ended vault (required by LoanBrokerSet) with a deposit booked,
    // advanced into the Investment phase, with a LoanBroker attached. The vault
    // version follows the amendments active in env. redemptionDate is when
    // VaultWithdraw becomes possible; tests that withdraw must advance past it.
    // Vault holds an Env&, so the struct is aggregate-initialized by
    // setupLendingVault.
    struct LendingFixture
    {
        test::jtx::Vault vault;
        Keylet vaultKeylet;
        Keylet brokerKeylet;
        NetClock::time_point redemptionDate;
        // The broker owner (loan counterparty) and the depositor, who borrows in
        // openLoan.
        test::jtx::Account owner;
        test::jtx::Account depositor;
    };

    // Cover rates default to 0%, so no cover deposit is needed; pass non-zero
    // values to exercise First-Loss Capital. scale overrides the default sfScale.
    static LendingFixture
    setupLendingVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset,
        Number const& depositAmount,
        std::chrono::seconds investmentWindow = std::chrono::seconds{1'000'000},
        std::optional<std::uint8_t> scale = std::nullopt,
        TenthBips32 coverRateMinimum = TenthBips32(0),
        TenthBips32 coverRateLiquidation = TenthBips32(0))
    {
        using namespace test::jtx;
        using namespace loan_broker;

        Vault const vault{env};
        auto [tx, vaultKeylet, subscriptionDate] = vault.createClosedEnded(
            {.owner = owner,
             .asset = asset,
             .subscriptionOffset = std::chrono::seconds{60},
             .investmentWindow = investmentWindow});
        if (scale)
            tx[sfScale] = *scale;
        env(tx);
        env.close();

        env(vault.deposit(
            {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(depositAmount)}));
        env.close();

        vault.closePastSubscription(subscriptionDate);

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(set(owner, vaultKeylet.key),
            kCoverRateMinimum(coverRateMinimum),
            kCoverRateLiquidation(coverRateLiquidation));
        env.close();

        return {
            .vault = vault,
            .vaultKeylet = vaultKeylet,
            .brokerKeylet = brokerKeylet,
            .redemptionDate = subscriptionDate + investmentWindow,
            .owner = owner,
            .depositor = depositor};
    }

    // Submits a LoanSet for principal against the fixture's broker, borrowed by
    // fixture.depositor and countersigned by fixture.owner, using the broker's
    // current LoanSequence. Interest defaults to 0%, so payments are pure
    // principal; flags pass through to LoanSet (e.g. tfLoanOverpayment).
    static Keylet
    openLoan(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        Number const& principal,
        std::uint32_t paymentTotal,
        TenthBips32 interestRate = TenthBips32(0),
        std::uint32_t flags = 0,
        std::uint32_t paymentInterval = 120,
        std::uint32_t gracePeriod = 60)
    {
        using namespace test::jtx;
        using namespace loan;

        auto const brokerSle = env.le(fixture.brokerKeylet);
        auto const loanKeylet = keylet::loan(
            fixture.brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        env(set(fixture.depositor, fixture.brokerKeylet.key, principal, flags),
            kInterestRate(interestRate),
            kGracePeriod(gracePeriod),
            kPaymentInterval(paymentInterval),
            kPaymentTotal(paymentTotal),
            Sig(sfCounterpartySignature, fixture.owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        return loanKeylet;
    }

    // Forwards to the shared checkFixedPrecisionVaultAssetsDeployed
    // (LoanTestBase.h): AssetsDeployed against broker and loan sums, stored against
    // derived AssetsTotal, and AssetsAvailable against the pseudo-account balance.
    void
    checkVaultLoanSums(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        std::vector<Keylet> const& loanKeylets,
        std::string const& label = {})
    {
        test::checkFixedPrecisionVaultAssetsDeployed(
            *this, env, fixture.vaultKeylet, {fixture.brokerKeylet}, loanKeylets, label);
    }
};

}  // namespace xrpl
