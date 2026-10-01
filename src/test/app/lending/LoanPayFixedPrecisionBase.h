#pragma once

#include <test/app/lending/LoanFixedPrecisionBase.h>
#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

// LoanPay-tier scaffolding on top of LoanFixedPrecisionBase: the
// coarsened-vault fixture (repeated late interest pushes a FixedPrecision
// vault's live Scale past its base grid) and the helpers built on it. Shared
// by LoanPayFixedPrecision and, via LoanManageFixedPrecisionBase, by
// LoanManageFixedPrecision.
class LoanPayFixedPrecisionBase : public LoanFixedPrecisionBase
{
protected:
    // Brings the Keylet overload into scope alongside the CoarsenedVault
    // overload added below; otherwise the derived overload would hide it.
    using LoanFixedPrecisionBase::checkVaultLoanSums;
    using VaultFixedPrecisionBase::expectCoarsened;

    // Coarsens a FixedPrecision vault through paid late interest, which the
    // LoanSet Open-zone guard does not bound. Deposits near the Open-zone ceiling,
    // originates a zero-interest loan for most of it, lets the first payment go
    // very late at the maximum LateInterestRate, and pays it late.
    //
    // extraLoans are originated before the coarsening loan goes late, because a
    // coarsened vault refuses further LoanSet. loanKeylets[0] is the coarsening
    // loan; loanKeylets[1..] are the extras in order.
    struct CoarsenedVault
    {
        LendingFixture fixture;
        std::vector<Keylet> loanKeylets;
    };

    // An extra loan originated before the vault coarsens, on top of the shared
    // zero-interest / 1-day-interval / 60s-grace defaults.
    struct ExtraLoan
    {
        Number principal;
        std::uint32_t flags = 0;
        std::uint32_t paymentTotal = 5;
        // Override with a long interval to keep the first payment from being overdue
        // once the clock advances to coarsen the vault.
        std::uint32_t paymentInterval = 24 * 60 * 60;
        // Defaults to the shared depositor. A dust-sized loan needs its own borrower
        // with a small balance: a sub-1e-9 debit from a multi-million balance does
        // not fit STAmount's 16 digits and rounds away.
        std::optional<test::jtx::Account> borrower = std::nullopt;
        // A flat per-payment service fee to the broker owner, giving a terminal
        // payment a zero vault-credit leg but a non-zero fee leg.
        Number serviceFee{0};
    };

    void
    checkVaultLoanSums(
        test::jtx::Env& env,
        CoarsenedVault const& coarsened,
        std::string const& label = {})
    {
        checkVaultLoanSums(env, coarsened.fixture, coarsened.loanKeylets, label);
    }

    struct CoarsenParams
    {
        std::uint8_t scale = 10;
        Number depositAmount{899'999};
        Number loanPrincipal{700'000};
        // The explicit initializer avoids the missing-field-initializers warning when a
        // designated initializer omits it.
        std::vector<ExtraLoan> extraLoans =  // NOLINT(readability-redundant-member-init)
            {};
        TenthBips32 coverRateMinimum = TenthBips32(0);
        TenthBips32 coverRateLiquidation = TenthBips32(0);
        Number coverDeposit{0};
        // 800 days pushes AssetsAvailable itself past its 16-digit budget, so cash
        // flows floor at a coarser grid. 200 days coarsens only getVaultScale()
        // (AA + AssetsDeployed), leaving AA's own grid at the base grid.
        int overdueDays = 800;
    };

    void
    expectCoarsened(test::jtx::Env const& env, CoarsenedVault const& coarsened)
    {
        expectCoarsened(env, coarsened.fixture.vaultKeylet);
    }

    // IOU setup for the coarsened-vault scenarios: the depositor holds 10M USD.
    static AssetAccounts
    setupCoarsenIou(test::jtx::Env& env, bool clawback = false)
    {
        return setupIou(
            env,
            {.clawback = clawback, .depositorTrust = 30'000'000, .depositorFunds = 10'000'000});
    }

    enum class Debit { Withdraw, Clawback };

    // Submits a VaultWithdraw (depositor) or VaultClawback (issuer) of amount and
    // returns how much AssetsAvailable dropped.
    Number
    debitAvailable(
        test::jtx::Env& env,
        CoarsenedVault const& coarsened,
        Debit kind,
        test::jtx::Account const& issuer,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset,
        Number const& amount)
    {
        auto const& vault = coarsened.fixture.vault;
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const before = snapshotVault(env, vaultKeylet, asset);
        if (kind == Debit::Withdraw)
        {
            env(vault.withdraw(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(amount)}));
        }
        else
        {
            env(vault.clawback(
                {.issuer = issuer,
                 .id = vaultKeylet.key,
                 .holder = depositor,
                 .amount = asset(amount).value()}));
        }
        env.close();
        return before.available - snapshotVault(env, vaultKeylet, asset).available;
    }

    CoarsenedVault
    coarsenVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset)
    {
        return coarsenVault(env, owner, depositor, asset, CoarsenParams{});
    }

    CoarsenedVault
    coarsenVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset,
        CoarsenParams const& params)
    {
        using namespace test::jtx;
        using namespace loan;
        using namespace loan_broker;
        using namespace lending;

        // A 15-year investment window, so extra loans with long payment intervals
        // still fit before RedemptionDate.
        auto fixture = setupLendingVault(
            env,
            owner,
            depositor,
            asset,
            params.depositAmount,
            std::chrono::seconds{473'040'000},
            params.scale,
            params.coverRateMinimum,
            params.coverRateLiquidation);

        if (params.coverDeposit != beast::kZero)
        {
            env(coverDeposit(owner, fixture.brokerKeylet.key, asset(params.coverDeposit).value()));
            env.close();
        }

        std::uint32_t const gracePeriod = 60;

        std::vector<Keylet> loanKeylets;
        std::vector<ExtraLoan> allLoans{ExtraLoan{.principal = params.loanPrincipal}};
        allLoans.insert(allLoans.end(), params.extraLoans.begin(), params.extraLoans.end());
        for (std::size_t i = 0; i < allLoans.size(); ++i)
        {
            auto const loanKeylet =
                keylet::loan(fixture.brokerKeylet.key, SeqProxy::rawSequence(i + 1));
            auto const& loan = allLoans[i];
            auto const submit = [&](auto&&... extra) {
                env(set(loan.borrower.value_or(depositor),
                        fixture.brokerKeylet.key,
                        loan.principal,
                        loan.flags),
                    kInterestRate(TenthBips32(0)),
                    kLoanServiceFee(loan.serviceFee),
                    kGracePeriod(gracePeriod),
                    kPaymentInterval(loan.paymentInterval),
                    kPaymentTotal(loan.paymentTotal),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    extra...);
            };
            if (i == 0)
            {
                // Only the coarsening loan needs a non-zero LateInterestRate.
                submit(kLateInterestRate(kMaxLateInterestRate));
            }
            else
            {
                submit();
            }
            env.close();
            loanKeylets.push_back(loanKeylet);
        }

        auto const loanSle = env.le(loanKeylets[0]);
        BEAST_EXPECT(loanSle);
        std::uint32_t const dueDate = loanSle ? loanSle->at(sfNextPaymentDueDate) : 0;

        // Leave the first payment overdue long enough that penalty interest pushes
        // AssetsAvailable itself, not just AA + AssetsDeployed, past its 16-digit budget at
        // the base scale; only then do cash credits floor.
        env.close(
            NetClock::time_point{NetClock::duration{dueDate + gracePeriod}} +
            std::chrono::seconds{params.overdueDays * 24 * 60 * 60});

        env(
            pay(depositor,
                loanKeylets[0].key,
                asset(params.loanPrincipal * 10).value(),
                tfLoanLatePayment));
        env.close();

        return {.fixture = fixture, .loanKeylets = loanKeylets};
    }

    // Reads an integral-asset vault Number field (AssetsAvailable, AssetsDeployed,
    // LossUnrealized, ... are STNumber fields, not plain UInt64 SFields) as a
    // std::uint64_t for plain integer arithmetic and search loops. Number
    // itself does not normalize to the asset's own zero exponent, so the value
    // is round-tripped through an STAmount of the vault's (integral) asset
    // first, matching how STAmount::mpt()/xrp() extract an exact int64.
    template <class T>
    static std::uint64_t
    toU64(SLE::ConstRef vault, TypedField<T> const& field)
    {
        Asset const asset = vault->at(sfAsset);
        STAmount const amount{asset, Number(vault->at(field))};
        assert(amount.exponent() == 0);
        return static_cast<std::uint64_t>(amount.mantissa());
    }

    // Doubles AssetsAvailable roughly cycles times via an origination/
    // repayment loop: originate a single-payment, kMaxInterestRate (100%),
    // one-year loan for the vault's entire current AssetsAvailable, then pay
    // it off in full at the due date. AssetsDeployed returns to zero after every
    // cycle, and the depositor's share count is never touched, so this grows
    // the per-share NAV purely through externally-funded interest -- the
    // "originate, repay, and delete...loans that grow the vault" step shared
    // by the sole-holder clawback and final-withdrawal scenarios below. The
    // depositor needs enough of their own funds to cover the interest leg of
    // every cycle (the principal itself round-trips: it is lent back out,
    // then repaid).
    void
    compoundVaultBySoleHolderLoanCycles(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset,
        int cycles)
    {
        using namespace test::jtx;
        using namespace loan;

        for (int i = 0; i < cycles; ++i)
        {
            auto const vaultSle = env.le(fixture.vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            Number const principal = vaultSle->at(sfAssetsAvailable);
            if (!BEAST_EXPECT(principal > beast::kZero))
                return;

            auto const loanKeylet = openLoan(
                env,
                fixture,
                principal,
                /* paymentTotal */ 1,
                percentageToTenthBips(100),
                /* flags */ 0,
                /* paymentInterval */ 31'536'000);

            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);
            env.close(NetClock::time_point{NetClock::duration{dueDate}} + std::chrono::seconds{1});

            Number const periodicPayment = loanSle->at(sfPeriodicPayment);
            std::int32_t const loanScale = loanSle->at(sfLoanScale);
            Number const paymentDue = roundPeriodicPayment(asset, periodicPayment, loanScale);

            // The clock is advanced to just past the due date, so the payment
            // must be flagged late (LateInterestRate is 0 by default, so this
            // adds no penalty -- it only satisfies the overdue check in
            // LendingHelpers' loanMakePayment).
            env(pay(depositor, loanKeylet.key, asset(paymentDue).value(), tfLoanLatePayment));
            env.close();
        }
    }
};

}  // namespace xrpl
