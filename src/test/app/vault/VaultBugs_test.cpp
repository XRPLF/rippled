#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultBugs_test : public VaultTestBase
{
private:
    // Bug: the equality check (vault outflow == destination inflow) was
    // skipped whenever the destination delta rounded to zero at localMinScale,
    // including cases where the vault outflow rounded to a non-zero value and
    // a representable amount of value was genuinely destroyed.
    //
    // Scenario: Bob's IOU balance sits 5 units below the 10^16 STAmount
    // precision boundary (atEdge2 = 9,999,999,999,999,995).  A withdrawal of
    // 6 USD shifts his balance across that boundary: the exponent increments
    // (0 → 1), so his effective inflow in Number space is only +5 — 1 USD is
    // consumed by the precision-boundary rounding and cannot be credited.
    //
    // The destroyed amount (1 USD) is sub-ULP at destinationScale=1 (step=10),
    // so the check treats it as an unavoidable IOU-precision artefact and
    // lets the transaction succeed.
    //
    // Contrast: if 15 USD were destroyed at the same scale (destroyed ≥ step),
    // floor(15/10)=1 ≠ 0 and the invariant would fire — that discrepancy IS
    // representable and indicates a real accounting bug.
    //
    // Pre-fixCleanup3_2_0: the "must increase destination balance" check fires
    // because roundedDestinationDelta = 0 ≤ 0.
    void
    testVaultWithdrawEqualityEnforced()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            STAmount const aliceLimit{usd.raw(), 2, 16};
            STAmount const bobLimit{usd.raw(), 2, 16};
            // Bob's balance sits 5 units below the 10^16 STAmount precision
            // boundary.  Receiving 6 USD shifts his exponent 0 → 1; the
            // STAmount records +5, not +6 (1 USD is lost to rounding).
            STAmount const atEdge2{usd.raw(), Number{9'999'999'999'999'995LL}};

            env(trust(alice, aliceLimit));
            env(trust(bob, bobLimit));
            env.close();

            env(pay(issuer, alice, usd(1'000)));
            env(pay(issuer, bob, atEdge2));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1'000)}));
            env.close();

            // Withdraw 6 USD to Bob: vault loses 6, Bob gains only 5.
            // Destroyed amount = 1 USD, which is sub-ULP at destinationScale=1.
            auto tx = vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = usd(6)});
            tx[sfDestination] = bob.human();
            env(tx, Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultWithdraw to destination at IOU precision boundary fires "
                "invariant (pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw to destination at IOU precision boundary succeeds "
                "when destroyed amount is sub-ULP (post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tesSUCCESS);
        }
    }

    // VaultDeposit by issuer with the vault parked at the IOU 16-digit
    // edge (9.999e15). Issuer mints 2 more USD; the vault trust line
    // goes 9.999e15 → 10^16, gaining 1 unit instead of 2 (canonicalization).
    //
    // Pre-fixCleanup3_2_0: the proactive check is absent; the deposit
    // applies, then VaultInvariant's "deposit must increase vault
    // balance" assertion fires at finalize time on the rounded vault
    // delta of zero, returning tecINVARIANT_FAILED.
    // Post-amendment: reject deposit that is not representable at Vault scale.
    void
    testBugIssuerVaultDepositAtEdge()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const owner{"owner"};

            env.fund(XRP(100'000), issuer, owner);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            STAmount const trustLimit{usd.raw(), 2, 16};
            STAmount const ownerFund{usd.raw(), Number{9'999'999'999'999'999LL}};

            env(trust(owner, trustLimit));
            env.close();
            env(pay(issuer, owner, ownerFund));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = owner, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();
            env(vault.deposit({.depositor = owner, .id = vaultKeylet.key, .amount = ownerFund}));
            env.close();

            // Vault pseudo-account is now at 9.999e15. Issuer mints 2
            // more USD. Pre: tecINVARIANT_FAILED at finalize. Post:
            // tecPRECISION_LOSS proactively. Either way, no value moves.
            env(vault.deposit({.depositor = issuer, .id = vaultKeylet.key, .amount = usd(2)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultDeposit by issuer at IOU edge fires "
                "tecINVARIANT_FAILED at finalize (pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit by issuer at IOU edge rejects with "
                "tecPRECISION_LOSS proactively (post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tecPRECISION_LOSS);
        }
    }

    // Bug: DeltaInfo::makeDelta uses max(scale(after), scale(before)) for
    // sfAssetsTotal/Available deltas.  This is symmetric to
    // testBugMakeDeltaAnteriorScale but in the opposite direction: a deposit
    // pushes assetsTotal from just below 1e16 (IOU exponent 0, ULP = 1) to just
    // above it (exponent 1, ULP = 10).  makeDelta picks the coarser *posterior*
    // scale 1.  The trust line balance rounds from atEdge + 2 = 10,000,000,000,000,001
    // → 1e16, so the pseudo-account delta is only +1 in IOU space.
    // roundToAsset(+1, scale=1) = 0 fires "deposit must increase vault balance"
    // even though the state change is consistent at every precision boundary.
    //
    // Fix (fixCleanup3_2_0): computeVaultMinScale uses the posterior Number-space
    // scale of sfAssetsTotal (which retains the full value 10,000,000,000,000,001,
    // exponent 0), giving minScale = 0.  roundToAsset(+1, scale=0) = 1 > 0 and
    // the invariant passes.  However the transactor's own precision guard fires
    // first (bob pays 2 USD, vault receives only 1 due to IOU rounding), so the
    // post-amendment result is tecPRECISION_LOSS rather than tesSUCCESS —
    // the depositor is protected from silently losing 1 USD to rounding.
    void
    testBugMakeDeltaPosteriorScale()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            // atEdge is the largest IOU value with exponent 0 (ULP = 1).
            // A deposit of 2 USD brings assetsTotal to 10,000,000,000,000,001
            // in Number space, crossing the 1e16 boundary in IOU space.
            STAmount const atEdge{usd.raw(), Number{9'999'999'999'999'999LL}};

            env(trust(alice, STAmount{usd.raw(), 2, 16}));
            env(trust(bob, usd(100)));
            env.close();
            env(pay(issuer, alice, atEdge));
            env(pay(issuer, bob, usd(2)));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // sfAssetsTotal = sfAssetsAvailable = atEdge (exponent 0, ULP = 1)
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = atEdge}));
            env.close();

            // Deposit 2 USD: +2 is sub-ULP at the posterior IOU scale (ULP = 10)
            // but exact at the Number scale retained by sfAssetsTotal.
            env(vault.deposit({.depositor = bob, .id = vaultKeylet.key, .amount = usd(2)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultDeposit across IOU scale boundary fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit across IOU scale boundary succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tecPRECISION_LOSS);
        }
    }

    // Bug: DeltaInfo::makeDelta uses max(scale(after), scale(before)) for the
    // sfAssetsTotal and sfAssetsAvailable deltas, and visitEntry applies the
    // same max() for the vault pseudo-account RippleState.  When
    // sfAssetsTotal sits exactly at 1e16 (IOU exponent 1, ULP = 10) and a
    // withdrawal of 5 USD brings it to 9.999...995e15 (IOU exponent 0,
    // ULP = 1), all three computations pick the anterior coarser scale 1.
    // roundToAsset(-5, scale=1) collapses to 0, so the invariant check
    // vaultPseudoDeltaAssets >= kZero fires even though the state change is
    // valid and fully consistent at IOU precision.
    //
    // Fix (fixCleanup3_2_0): finalize compares the vault pseudo-account and
    // sfAssetsTotal/Available deltas directly in Number space, bypassing
    // scale-coarsened rounding.
    void
    testBugMakeDeltaAnteriorScale()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};

            env.fund(XRP(100'000), issuer, alice);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            // Trust limit of 2e16, fund exactly 1e16 so deposit lands at the
            // IOU scale-1 boundary (exponent 1, ULP = 10).
            STAmount const fundAndDeposit{usd.raw(), Number{1, 16}};

            env(trust(alice, STAmount{usd.raw(), 2, 16}));
            env.close();
            env(pay(issuer, alice, fundAndDeposit));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // sfAssetsTotal = sfAssetsAvailable = 1e16 (exponent 1, ULP = 10).
            env(vault.deposit(
                {.depositor = alice, .id = vaultKeylet.key, .amount = fundAndDeposit}));
            env.close();

            // Withdraw 5 USD: -5 is sub-ULP at the anterior scale (ULP = 10)
            // but exact at the posterior scale (ULP = 1).  The state change is
            // consistent; only the invariant's scale selection is wrong.
            env(vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = usd(5)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultWithdraw across IOU scale boundary fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw across IOU scale boundary succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tesSUCCESS);
        }
    }

    // Bug: when a depositor's IOU trustline balance is very large (e.g.
    // ~1e17), adding a small deposit (e.g. 1 USD) leaves sfAssetsTotal
    // unchanged at IOU precision because the increment is sub-ULP at the
    // vault's current asset scale.  The vault records the deposit, mints
    // shares, and decrements the depositor's trustline, but sfAssetsTotal
    // does not change — the conservation invariant fires because the rail
    // delta is zero.
    //
    // Two sub-cases are exercised:
    //   1. First-ever deposit into an empty vault: the depositor's own
    //      trustline has a large balance so 1 USD canonicalizes to zero
    //      when written back through the IOU rail.
    //   2. Subsequent deposit after the vault already holds a large
    //      sfAssetsTotal: a different depositor (bob, with a small balance)
    //      sends 1 USD, which again rounds to zero at the vault's coarse
    //      asset scale.
    //
    // Fix (fixCleanup3_2_0): the deposit transactor checks whether
    // roundToAsset(amount, vault_scale) == 0 and rejects early with
    // tecPRECISION_LOSS before any state is modified.
    void
    testVaultDepositCanonicalizeToZero()
    {
        using namespace test::jtx;
        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();

            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};

            STAmount const trustLimit{usd.raw(), Number{99'999'999'999'999'999LL}};
            STAmount const aliceFund{usd.raw(), Number{99'999'999'999'999'999LL}};

            env(trust(alice, trustLimit));
            env(trust(bob, trustLimit));
            env.close();

            env(pay(issuer, alice, aliceFund));
            env(pay(issuer, bob, usd(1000)));
            env.close();

            Vault const vault{env};

            // Scale=0 so sfAssetsTotal stores whole USD
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // Alice's deposit canonicalizes to zero at her own trustline scale
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1)}),
                Ter(expected));

            // Increase vault-scale
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = aliceFund}));
            env.close();

            env(vault.deposit({.depositor = bob, .id = vaultKeylet.key, .amount = usd(1)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultDeposit below Vault precision canonicalized to zero "
                "(pre-fixCleanup3_2_0)");
            // Also remove fixCleanup3_4_0 so the VaultDeposit clamp
            // introduced by that amendment does not short-circuit this
            // pre-fixCleanup3_2_0 scenario with tecPRECISION_LOSS.
            runScenario(
                testableAmendments() - fixCleanup3_2_0 - fixCleanup3_4_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit below Vault precision canonicalized to zero "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tecPRECISION_LOSS);
        }
    }

    // Bug: ValidVault::visitEntry computes destinationDelta.scale as
    // max(before_exponent, after_exponent) for RippleState entries.  When a
    // withdrawal credits a destination whose IOU balance sits just below a
    // power-of-10 boundary (atEdge = 9'999'999'999'999'999), the post-credit
    // STAmount rounds up one exponent (exponent 0 → 1), making
    // destinationDelta.scale = 1.  The invariant then calls
    // roundToAsset(+2 USD, scale=1) = 0 and incorrectly fires
    // "withdrawal must increase destination balance".
    //
    // Fix (fixCleanup3_2_0): finalize compares destination delta directly in
    // Number space, bypassing scale-coarsened rounding.  The transaction
    // itself succeeds because the effective IOU credit is non-trivial at
    // Number precision even though the STAmount exponent shifted.
    void
    testVaultWithdrawCanonicalizeToZero()
    {
        using namespace test::jtx;

        enum class DestKind : bool { ThirdParty = false, Self = true };

        auto runScenario = [this](FeatureBitset features, DestKind destKind, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            STAmount const aliceLimit{usd.raw(), 2, 16};
            STAmount const bobLimit{usd.raw(), 2, 16};
            STAmount const atEdge{usd.raw(), Number{9'999'999'999'999'999LL}};

            env(trust(alice, aliceLimit));
            if (destKind == DestKind::ThirdParty)
                env(trust(bob, bobLimit));
            env.close();

            env(pay(issuer, alice, usd(1'000)));
            if (destKind == DestKind::ThirdParty)
                env(pay(issuer, bob, atEdge));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1'000)}));
            env.close();

            // For the self-destination case, push alice's own trust line to
            // the IOU edge so the next withdraw inflow crosses the boundary.
            if (destKind == DestKind::Self)
            {
                env(pay(issuer, alice, atEdge));
                env.close();
            }

            auto tx = vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = usd(2)});
            if (destKind == DestKind::ThirdParty)
                tx[sfDestination] = bob.human();
            env(tx, Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultWithdraw to third-party at IOU edge fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(
                testableAmendments() - fixCleanup3_2_0, DestKind::ThirdParty, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw to third-party at IOU edge succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), DestKind::ThirdParty, tesSUCCESS);
        }
        {
            testcase(
                "bug: VaultWithdraw to self at IOU edge fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(
                testableAmendments() - fixCleanup3_2_0, DestKind::Self, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw to self at IOU edge succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), DestKind::Self, tesSUCCESS);
        }
    }

    // Bug: a debit can be genuinely non-zero yet still be dust relative to a
    // sfAssetsTotal/sfAssetsAvailable large enough to exceed STAmount's precision, e.g.
    // AssetsTotal 2e12 minus a 1e-6 debit needs 19 significant digits and rounds straight
    // back to 2e12. The shares still move, so ValidVault later fails with "must decrease
    // vault balance" instead of a clean upfront rejection.
    //
    // Fix (fixCleanup3_4_0): reject upfront with tecPRECISION_LOSS if the debit would
    // canonicalize back to the prior stored value.
    //
    // With a single depositor AssetsTotal == AssetsAvailable, so both
    // debitIsNonZeroDust operands trip together here. LoanRounding_test's
    // "dust debit vs AssetsTotal only" case isolates the AssetsTotal operand
    // via a heavily-loaned vault.
    void
    testBugVaultDustDebitCanonicalizesToNoOp()
    {
        using namespace test::jtx;

        // Fund a single depositor and have them deposit `total` USD in one shot (default
        // scale 6, so shares mint at exactly total*1e6).
        auto const seedVault = [](Env& env, Number const& total) {
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const holder{"holder"};

            env.fund(XRP(1'000'000), issuer, owner, holder);
            env.close();
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            env(trust(holder, usd(100'000'000'000'000LL)));
            env.close();
            env(pay(issuer, holder, usd(total)));
            env.close();

            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = owner, .asset = usd.raw()});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = holder, .id = keylet.key, .amount = usd(total)}),
                Ter(tesSUCCESS));
            env.close();

            return keylet;
        };

        {
            auto runScenario = [&](FeatureBitset features, TER expected) {
                Env env(*this, features);
                Number const total{2, 12};
                auto const keylet = seedVault(env, total);

                Account const issuer{"issuer"};
                PrettyAsset const usd{issuer["USD"]};

                // 1 share's worth of assets: 1e-6, below AssetsTotal's storage precision.
                env(Vault::clawback(
                        {.issuer = issuer,
                         .id = keylet.key,
                         .holder = Account{"holder"},
                         .amount = usd(Number{1, -6}).value()}),
                    Ter(expected));
                env.close();
            };

            testcase("bug: VaultClawback dust debit fires invariant (pre-fixCleanup3_4_0)");
            runScenario(all_ - fixCleanup3_4_0, tecINVARIANT_FAILED);
            testcase("bug: VaultClawback dust debit rejected cleanly (post-fixCleanup3_4_0)");
            runScenario(all_, tecPRECISION_LOSS);
        }

        {
            auto runScenario = [&](FeatureBitset features, TER expected) {
                Env env(*this, features);
                Number const total{2, 12};
                auto const keylet = seedVault(env, total);

                MPTIssue const share{env.le(keylet)->at(sfShareMPTID)};

                // Redeem 1 share, worth 1e-6 assets, below AssetsTotal's storage precision.
                env(Vault::withdraw(
                        {.depositor = Account{"holder"},
                         .id = keylet.key,
                         .amount = STAmount{share, 1}}),
                    Ter(expected));
                env.close();
            };

            testcase("bug: VaultWithdraw dust debit fires invariant (pre-fixCleanup3_4_0)");
            runScenario(all_ - fixCleanup3_4_0, tecINVARIANT_FAILED);
            testcase("bug: VaultWithdraw dust debit rejected cleanly (post-fixCleanup3_4_0)");
            runScenario(all_, tecPRECISION_LOSS);
        }
    }

    // Bug: an IOU VaultDeposit can credit the vault (and its pseudo-account trust line) with
    // more than the depositor paid, when the exact new total needs 17 significant digits but
    // STAmount only keeps 16. The pseudo-account trust line (accountSend) and sfAssetsTotal
    // (associateAsset) both independently round the 17-digit sum UP to the nearest 16-digit
    // value -- the same direction on both rails, so no invariant catches the mismatch.
    //
    // seed 9.999999999999999 at scale 15 (1 share = 1e-15 asset); depositing 5 buys exactly
    // 5e15 shares.
    //   exact new total   9.999999999999999 + 5 = 14.999999999999999   (17 digits)
    //   stored            15                        (rounds UP to the nearest 16-digit value)
    //   credited          more than the depositor actually paid in -- value issued out of
    //                      nothing
    //
    // Fix (fixCleanup3_4_0): VaultDeposit rounds the credited amount DOWN to what's exactly
    // representable at the vault's sfAssetsTotal scale before storing it, so the vault (and
    // its pseudo-account) can never be credited more than the depositor paid.
    void
    testBugVaultDepositOvercreditsAcrossScaleBoundary()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, bool expectOvercredit) {
            Env env(*this, features);
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            Account const depositor{"depositor"};
            env.fund(XRP(1'000'000), owner, issuer, depositor);
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            Number const seed{9'999'999'999'999'999LL, -15};
            Number const deposit{5};

            env(trust(depositor, usd(1'000'000'000)));
            env.close();
            env(pay(issuer, depositor, usd(deposit)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = usd.raw()});
            tx[sfScale] = 15;
            env(tx);
            env.close();
            env(vault.deposit({.depositor = issuer, .id = keylet.key, .amount = usd(seed)}));
            env.close();

            Number const totalBefore = env.le(keylet)->at(sfAssetsTotal);
            Number const depositorBefore = env.balance(depositor, usd.raw()).number();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = usd(deposit)}));
            env.close();

            Number const totalAfter = env.le(keylet)->at(sfAssetsTotal);
            Number const depositorAfter = env.balance(depositor, usd.raw()).number();
            Number const paid = depositorBefore - depositorAfter;
            Number const credited = totalAfter - totalBefore;

            if (expectOvercredit)
            {
                BEAST_EXPECTS(
                    credited > paid,
                    "AssetsTotal credited " + to_string(credited) + " for a payment of " +
                        to_string(paid) + ", expected an overcredit");
            }
            else
            {
                BEAST_EXPECTS(
                    credited <= paid,
                    "AssetsTotal credited " + to_string(credited) + " for a payment of " +
                        to_string(paid));
            }
        };

        // Also remove fixCleanup3_2_0: its roundToVaultScale trims the requested amount
        // down to the vault's current scale before this bug's own 17-digit-sum boundary is
        // ever reached, which would otherwise mask the overcredit behind a different
        // (already-fixed) rounding path instead of reproducing it.
        testcase(
            "bug: VaultDeposit overcredits across an IOU scale boundary "
            "(pre-fixCleanup3_4_0)");
        runScenario(all_ - fixCleanup3_2_0 - fixCleanup3_4_0, true);

        testcase(
            "bug: VaultDeposit no longer overcredits across an IOU scale boundary "
            "(post-fixCleanup3_4_0)");
        runScenario(all_, false);
    }

    // Bug: a partial VaultWithdraw can permanently lock a large IOU vault. IOU amounts hold
    // 16 significant digits, so once sfAssetsTotal grows large enough it's stored in steps of
    // 1 ULP (e.g. steps of 100 at 1e17). If a single share is worth less than that step,
    // withdrawing all-but-one share overpays: the payout rounds UP to the entire remaining
    // balance, draining sfAssetsTotal to exactly zero while the last share stays outstanding.
    // The vault is then insolvent (AssetsTotal == 0, sharesTotal > 0) and permanently stuck --
    // withdrawing the last share pays 0 and can't change the balance (tecINVARIANT_FAILED),
    // depositing to refill is blocked because the vault is insolvent (tecLOCKED), and deleting
    // is impossible while a share is outstanding (tecHAS_OBLIGATIONS). A full withdrawal from
    // the start would have emptied the vault cleanly; once this partial-withdrawal state is
    // reached, no sequence of ordinary transactions recovers it.
    //
    // Fix (fixCleanup3_4_0): VaultWithdraw rounds the payout DOWN to what's exactly
    // representable at the vault's sfAssetsTotal scale, so a partial withdrawal can never
    // drain the vault to zero while shares remain outstanding.
    void
    testBugVaultLockedByPartialWithdraw()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            Env env(*this, features);
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            env.fund(XRP(1'000'000), owner, issuer, holder);
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            env(trust(holder, usd(Number{1, 18})));
            env.close();
            env(pay(issuer, holder, usd(Number{1, 17})));
            env.close();

            Vault const vault{env};
            // scale=0: 1 share == 1 asset unit, so sharesTotal == assetsTotal == 1e17.
            auto [tx, keylet] = vault.create({.owner = owner, .asset = usd.raw()});
            tx[sfScale] = 0;
            env(tx);
            env.close();
            env(vault.deposit(
                {.depositor = holder, .id = keylet.key, .amount = usd(Number{1, 17})}));
            env.close();

            MPTIssue const share{env.le(keylet)->at(sfShareMPTID)};
            std::int64_t const allButOne = 100'000'000'000'000'000LL - 1;
            env(vault.withdraw(
                {.depositor = holder, .id = keylet.key, .amount = STAmount{share, allButOne}}));
            env.close();

            // The holder now tries to withdraw the one remaining share. Pre-fix, the vault is
            // already insolvent (AssetsTotal == 0, one share outstanding) and this fails with
            // tecINVARIANT_FAILED forever. Post-fix, the earlier partial withdrawal never
            // drained the vault, so this succeeds and empties it cleanly.
            env(vault.withdraw(
                    {.depositor = holder, .id = keylet.key, .amount = STAmount{share, 1}}),
                Ter(expected));
            env.close();
        };

        testcase(
            "bug: VaultWithdraw permanently locks a large IOU vault "
            "(pre-fixCleanup3_4_0)");
        runScenario(all_ - fixCleanup3_4_0, tecINVARIANT_FAILED);
        testcase(
            "bug: VaultWithdraw no longer locks a large IOU vault "
            "(post-fixCleanup3_4_0)");
        runScenario(all_, tesSUCCESS);
    }

    // VaultDeposit::preclaim uses accountHolds(..., SpendableHandling::
    // shFULL_BALANCE), which for an IOU asset adds the counterparty's
    // LowLimit/HighLimit to the depositor's raw balance (TokenHelpers.cpp:
    // getTrustLineBalance with includeOppositeLimit=true). When the
    // depositor's raw balance < deposit amount but raw + opposite limit >=
    // amount, preclaim is satisfied. doApply then calls
    // directSendNoFeeIOU, which unconditionally subtracts saAmount from
    // saBalance — driving the trust line negative — and returns tesSUCCESS.
    // The post-send sanity check uses the default shSIMPLE_BALANCE (no
    // opposite-limit add), sees a negative balance, and returns tefINTERNAL.
    void
    testVaultDepositNegativeBalanceFromOppositeLimit()
    {
        auto runTest = [&](FeatureBitset f, TER expected) {
            using namespace test::jtx;
            using namespace std::literals;

            Env env{*this, f};
            Account const gw{"gateway"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};

            env.fund(XRP(10000), gw, owner, depositor);
            env.close();

            // Gateway with DefaultRipple so vault creation on its IOU works.
            env(fset(gw, asfDefaultRipple));
            env.close();

            // Depositor opens a trust line to gateway and receives a small
            // balance.
            PrettyAsset const usd = gw["USD"];
            env.trust(usd(1000), depositor);
            env(pay(gw, depositor, usd(100)));  // raw trust-line balance: 100
            env.close();

            // Key precondition: gateway sets a non-zero limit on the same
            // RippleState — the "opposite field" from depositor's perspective.
            // This is what inflates shFULL_BALANCE in preclaim above the raw
            // balance.
            env(trust(gw, depositor["USD"](1000)));
            env.close();

            // Create the IOU vault.
            Vault const vault{env};
            auto [vaultTx, keylet] = vault.create({.owner = owner, .asset = usd});
            env(vaultTx);
            env.close();

            // Submit a deposit of 500 USD:
            //   - raw balance:                100 USD
            //   - opposite limit (gw's side): 1000 USD
            //   - preclaim sees 100 + 1000 = 1100, passes (>= 500)
            //   - doApply transfers 500, depositor's trust-line balance
            //     becomes -400
            //   - sanity check at VaultDeposit.cpp:256 fires
            //   - tx returns tefINTERNAL (BUG — should be tesSUCCESS.
            auto depositTx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = usd(500)});
            env(depositTx, Ter(expected));
            env.close();
        };

        {
            testcase(
                "IOU vault deposit exceeding depositor's balance but "
                "within counterparty's trust limit, pre-fixCleanup3_2_0 "
                "(tefINTERNAL)");
            runTest(test::jtx::testableAmendments() - fixCleanup3_2_0, tefINTERNAL);
        }
        {
            testcase(
                "IOU vault deposit exceeding depositor's balance but "
                "within counterparty's trust limit, post-fixCleanup3_2_0 "
                "(tesSUCCESS)");
            runTest(test::jtx::testableAmendments(), tesSUCCESS);
        }
    }

    // Reproduction: canWithdraw IOU limit check bypassed when
    // withdrawal amount is specified in shares (MPT) rather than in assets.
    void
    testBug6LimitBypassWithShares()
    {
        using namespace test::jtx;
        testcase("Bug6 - limit bypass with share-denominated withdrawal");

        auto const allAmendments = testableAmendments() | featureSingleAssetVault;

        for (auto const& features : {allAmendments, allAmendments - fixCleanup3_1_3})
        {
            bool const withFix = features[fixCleanup3_1_3];

            Env env{*this, features};
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            Account const depositor{"depositor"};
            Account const charlie{"charlie"};
            Vault const vault{env};

            env.fund(XRP(1000), issuer, owner, depositor, charlie);
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1000), owner);
            env.trust(asset(1000), depositor);
            env(pay(issuer, owner, asset(200)));
            env(pay(issuer, depositor, asset(200)));
            env.close();

            // Charlie gets a LOW trustline limit of 5
            env.trust(asset(5), charlie);
            env.close();

            auto const [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const depositTx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(depositTx);
            env.close();

            // Get the share MPT info
            auto const vaultSle = env.le(keylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            auto const mptIssuanceID = vaultSle->at(sfShareMPTID);
            MPTIssue const shares(mptIssuanceID);
            PrettyAsset const share(shares);

            // CONTROL: Withdraw 10 IOU (asset-denominated) to charlie.
            // Charlie's limit is 5, so this should be rejected with tecNO_LINE
            // regardless of the amendment.
            {
                auto withdrawTx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(10)});
                withdrawTx[sfDestination] = charlie.human();
                env(withdrawTx, Ter{tecNO_LINE});
                env.close();
            }
            auto const charlieBalanceBefore = env.balance(charlie, asset.raw().get<Issue>());

            // Withdraw the equivalent amount in shares to charlie.
            // Post-fix: rejected (tecNO_LINE) because the share amount is
            //   converted to assets and the trustline limit is checked.
            // Pre-fix: succeeds (tesSUCCESS) because the limit check was
            //   skipped for share-denominated withdrawals.
            {
                auto withdrawTx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = STAmount(share, 10'000'000)});
                withdrawTx[sfDestination] = charlie.human();
                env(withdrawTx, Ter{withFix ? TER{tecNO_LINE} : TER{tesSUCCESS}});
                env.close();

                auto const charlieBalanceAfter = env.balance(charlie, asset.raw().get<Issue>());
                if (withFix)
                {
                    // Post-fix: charlie's balance is unchanged — the withdrawal
                    // was correctly rejected despite being share-denominated.
                    BEAST_EXPECT(charlieBalanceAfter == charlieBalanceBefore);
                }
                else
                {
                    // Pre-fix: charlie received the assets, bypassing the
                    // trustline limit.
                    BEAST_EXPECT(charlieBalanceAfter > charlieBalanceBefore);
                }
            }
        }
    }

public:
    void
    run() override
    {
        testVaultWithdrawEqualityEnforced();
        testBugIssuerVaultDepositAtEdge();
        testBugMakeDeltaPosteriorScale();
        testBugMakeDeltaAnteriorScale();
        testVaultDepositCanonicalizeToZero();
        testVaultWithdrawCanonicalizeToZero();
        testBugVaultDustDebitCanonicalizesToNoOp();
        testBugVaultDepositOvercreditsAcrossScaleBoundary();
        testBugVaultLockedByPartialWithdraw();
        testVaultDepositNegativeBalanceFromOppositeLimit();
        testBug6LimitBypassWithShares();
    }
};

BEAST_DEFINE_TESTSUITE(VaultBugs, app, xrpl);

}  // namespace xrpl
