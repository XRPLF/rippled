#pragma once

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
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <optional>

namespace xrpl::test {

// Shared fixture for VaultInvariantPrecision_test and
// VaultTransactorPrecision_test.
//
// Layout:
//   - A-1 (impairAndPaySibling=false): 1000 USD vault + one ordinary loan.
//     assetsTotal ~= 1000.353..., assetsAvailable == 993, lossUnrealized == 0.
//   - A-3 (impairAndPaySibling=true):  add a second loan of principal 11,
//     impair the first loan, and pay off the second in full.  This drives
//     the vault to the lossUnrealized == (assetsTotal - assetsAvailable)
//     boundary where the loss invariant used to spuriously fire.
class VaultPrecisionFixture : public LoanTestBase
{
protected:
    static constexpr std::uint32_t kFixturePaymentInterval = 86400u * 30u;
    static constexpr std::uint32_t kFixtureGracePeriod = 86400u * 30u;
    static constexpr std::uint32_t kFixturePaymentTotal = 120u;
    // 10% APR, expressed in tenth-bips (1000 = 10.00 %).
    static constexpr std::uint32_t kFixtureInterestTenthBips = 1000u;

    struct Fixture
    {
        // Every account is initialised with a placeholder name because
        // jtx::Account has no default constructor; setupSingleLoanVault
        // overwrites them.
        jtx::Account issuer{"vp_issuer_placeholder"};
        jtx::Account lender{"vp_lender_placeholder"};
        jtx::Account borrower{"vp_borrower_placeholder"};
        // Distinct account used to deposit into the vault. Keeps share
        // ownership independent of the initial vault seeding.
        jtx::Account depositor{"vp_depositor_placeholder"};
        // Optional so callers can BEAST_EXPECT(f.asset && f.broker)
        // after setup; both are populated in the happy path.
        std::optional<jtx::PrettyAsset> asset;
        std::optional<BrokerInfo> broker;
        // Keylet has no default constructor. Fill with an obviously
        // meaningless placeholder; setupSingleLoanVault overwrites the
        // fields that matter.
        Keylet vaultKeylet{ltACCOUNT_ROOT, uint256{}};
        Keylet loan1Keylet{ltACCOUNT_ROOT, uint256{}};
        // Only meaningful when impairAndPaySibling == true.
        Keylet loan2Keylet{ltACCOUNT_ROOT, uint256{}};
        jtx::Account vaultAccount{"vp_vault_pseudo_placeholder"};
        MPTID share;
    };

    // Read-only snapshot of the vault + share issuance at a point in time.
    // Uses Number for exact arithmetic (no re-quantization).
    struct Numbers
    {
        Asset asset;
        MPTIssue share;
        Number assetsTotal{};      // sfAssetsTotal
        Number assetsAvailable{};  // sfAssetsAvailable
        Number lossUnrealized{};   // sfLossUnrealized
        Number pseudo{};           // vault pseudo-account balance in the asset
        Number sharesTotal{};      // sfOutstandingAmount on the share MPT
    };

    static Numbers
    read(jtx::Env const& env, Fixture const& f)
    {
        Numbers n{.asset = f.asset ? f.asset->raw() : Asset{}, .share = MPTIssue{f.share}};
        if (auto const vaultSle = env.le(f.vaultKeylet))
        {
            n.assetsTotal = vaultSle->at(sfAssetsTotal);
            n.assetsAvailable = vaultSle->at(sfAssetsAvailable);
            n.lossUnrealized = vaultSle->at(sfLossUnrealized);
        }
        if (auto const issuanceSle = env.le(keylet::mptokenIssuance(f.share)))
        {
            n.sharesTotal = issuanceSle->at(sfOutstandingAmount);
        }
        if (f.asset)
            n.pseudo = env.balance(f.vaultAccount, *f.asset).number();
        return n;
    }

    // One unit at the STAmount scale of `assetsTotalAfter`.  Used as the
    // tolerance in one-unit-band assertions.
    static Number
    oneUnit(Asset const& asset, Number const& assetsTotalAfter)
    {
        return Number{1, scale(assetsTotalAfter, asset)};
    }

    // Build the shared vault + loan(s) layout.  The caller constructs
    // `env` with whatever FeatureBitset they want to exercise; this helper
    // just uses it.  If `allowClawback` is true, the issuer's
    // asfAllowTrustLineClawback flag is set BEFORE any trust line is
    // established for that issuer.  A separate env.close() runs so the
    // flag lands in the ledger before the trust lines are set up.
    static Fixture
    setupSingleLoanVault(jtx::Env& env, bool impairAndPaySibling, bool allowClawback = false)
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace jtx::loan_broker;

        Fixture f;
        f.issuer = Account{"vp_issuer"};
        f.lender = Account{"vp_lender"};
        f.borrower = Account{"vp_borrower"};
        f.depositor = Account{"vp_depositor"};

        env.fund(XRP(1'000'000), f.issuer, f.lender, f.borrower, f.depositor);
        env.close();

        // Must be set BEFORE any trust line to `issuer` is created.
        if (allowClawback)
        {
            env(fset(f.issuer, asfAllowTrustLineClawback));
            env.close();
        }

        PrettyAsset const asset = f.issuer["USD"];
        f.asset = asset;

        env.trust(asset(1'000'000'000), f.lender);
        env.trust(asset(1'000'000'000), f.borrower);
        env.trust(asset(1'000'000'000), f.depositor);
        env(pay(f.issuer, f.lender, asset(100'000'000)));
        env(pay(f.issuer, f.borrower, asset(100'000'000)));
        env(pay(f.issuer, f.depositor, asset(100'000'000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = 0,
            .coverRateMin = percentageToTenthBips(1),
            .coverDeposit = 10'000,
            .managementFeeRate = TenthBips16{100},
            .coverRateLiquidation = xrpl::lending::kMaxCoverRate};

        // Build the vault + broker manually (rather than calling
        // createVaultAndBroker) so we can seed only the lender/depositor
        // trust lines we set up above, and skip the LoanTestBase auto
        // funding that assumes an XRP asset.
        Vault const vault{env};
        auto [createTx, vaultKeylet] = vault.create({.owner = f.lender, .asset = asset});
        env(createTx);
        env.close();
        f.vaultKeylet = vaultKeylet;

        env(vault.deposit(
            {.depositor = f.lender,
             .id = vaultKeylet.key,
             .amount = asset(brokerParams.vaultDeposit)}));
        env.close();

        auto const brokerKeylet =
            keylet::loanBroker(f.lender.id(), SeqProxy::rawSequence(env.seq(f.lender)));

        env(set(f.lender, vaultKeylet.key, brokerParams.flags),
            kManagementFeeRate(brokerParams.managementFeeRate),
            kDebtMaximum(asset(brokerParams.debtMax).value()),
            kCoverRateMinimum(brokerParams.coverRateMin),
            kCoverRateLiquidation(TenthBips32(brokerParams.coverRateLiquidation)));
        env(coverDeposit(f.lender, brokerKeylet.key, asset(brokerParams.coverDeposit).value()));
        env.close();

        f.broker = BrokerInfo{asset, brokerKeylet, vaultKeylet, brokerParams};

        auto const vaultSle = env.le(vaultKeylet);
        f.vaultAccount = Account{"vp_vault_pseudo", vaultSle->at(sfAccount)};
        f.share = vaultSle->at(sfShareMPTID);

        Fee const bigFee{env.current()->fees().base * 200};

        auto const setLoan = [&](Number const& principal) -> Keylet {
            auto const brokerSle = env.le(brokerKeylet);
            auto const loanKeylet = keylet::loan(
                brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
            env(loan::set(f.borrower, brokerKeylet.key, asset(principal).number()),
                Sig(sfCounterpartySignature, f.lender),
                jtx::loan::kInterestRate(TenthBips32{kFixtureInterestTenthBips}),
                jtx::loan::kPaymentTotal(kFixturePaymentTotal),
                jtx::loan::kPaymentInterval(kFixturePaymentInterval),
                jtx::loan::kGracePeriod(kFixtureGracePeriod),
                bigFee);
            env.close();
            return loanKeylet;
        };

        // Loan 1: principal 7, the one ordinary loan in both fixtures.
        // With vault deposit 1000, this leaves A ≈ 993 (see plan).
        f.loan1Keylet = setLoan(Number{7});

        if (!impairAndPaySibling)
            return f;

        // Loan 2: sibling loan of principal 11.
        f.loan2Keylet = setLoan(Number{11});

        // Pay off loan 2 in full so its total value flows into the vault
        // and pushes T-A upward, meeting the residual loss.  Generous
        // upper bound; the transactor takes only what is due.
        //
        // This happens before the impair below because impair under
        // fixCleanup3_4_0 requires loan 1 to already be late, and the two
        // loans are originated close enough together that advancing past
        // loan 1's due date also makes loan 2 late — which would reject
        // this full payment with tecEXPIRED.
        auto const payoff = asset(Number{50}).value();
        env(pay(f.borrower, f.loan2Keylet.key, payoff, tfLoanFullPayment), bigFee);
        env.close();

        // Impair loan 1 → drives sfLossUnrealized to loan 1's value.
        if (env.current()->rules().enabled(fixCleanup3_4_0))
        {
            std::uint32_t const dueDate = env.le(f.loan1Keylet)->at(sfNextPaymentDueDate);
            env.close(NetClock::time_point{NetClock::duration{dueDate}} + std::chrono::seconds{1});
        }

        env(jtx::loan::manage(f.lender, f.loan1Keylet.key, tfLoanImpair), bigFee);
        env.close();

        return f;
    }
};

}  // namespace xrpl::test
