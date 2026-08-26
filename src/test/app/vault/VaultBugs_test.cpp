#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
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
            // fixCleanup3_4_0 has to be off as well: its depositor-side check
            // rejects alice's deposit for the same reason, so the invariant is
            // only reachable with neither guard in place.
            testcase(
                "bug: VaultDeposit below Vault precision canonicalized to zero "
                "(pre-fixCleanup3_2_0)");
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

    // A deposit does not transfer the requested amount. It transfers the
    // request truncated to a whole number of shares and converted back, which
    // can be strictly smaller. When that smaller value is below half a ULP at
    // the depositor's own trust-line scale, the debit rounds away to nothing:
    // the depositor pays nothing, while the vault books the assets and mints
    // shares. ValidVault catches the desync at finalize time.
    //
    // Only a non-power-of-ten assets-to-shares ratio is needed, and that
    // happens through ordinary use: LoanPay books accrued interest into
    // sfAssetsTotal without minting shares.
    //
    // The fixCleanup3_2_0 guard in preclaim does not help, because it tests the
    // raw requested amount, which is large enough to survive the rounding.
    // Post-fixCleanup3_4_0 the post-truncation value is checked as well and the
    // deposit is rejected with tecPRECISION_LOSS before anything moves.
    void
    testBugDepositShareTruncationSubUlp()
    {
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        // How bob's trust line is set up before he deposits. Holding is the plain case: a large
        // positive balance whose ULP swallows the debit. InDebt is the case where the stored
        // balance and the spendable amount diverge: bob owes the issuer 1e16, and the issuer's
        // limit on the same line lets him spend 1000 anyway. Reading the spendable amount there
        // reports a small, finely scaled number, while the rounding of the debit is still governed
        // by the 1e16 he actually holds.
        enum class Line { Holding, InDebt };

        auto runScenario = [this](FeatureBitset features, Line line, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, carol, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            PrettyAsset const bobUsd{bob["USD"]};
            STAmount const trustLimit{usd.raw(), Number{99'999'999'999'999'999LL}};
            // Bob's balance sits exactly on a multiple-of-10 boundary at the
            // 1e16 IOU precision cusp, where one ULP is 10.
            STAmount const bobEdge{usd.raw(), Number{10'000'000'000'000'010LL}};
            STAmount const bobDebt{bobUsd.raw(), Number{10'000'000'000'000'000LL}};
            STAmount const oppositeLimit{bobUsd.raw(), Number{10'000'000'000'001'000LL}};

            env(trust(alice, trustLimit));
            env(trust(carol, trustLimit));
            env(trust(bob, trustLimit));
            env.close();

            env(pay(issuer, alice, usd(1'000)));
            env(pay(issuer, carol, usd(1'000)));
            if (line == Line::Holding)
            {
                env(pay(issuer, bob, bobEdge));
            }
            else
            {
                // The issuer trusts bob's own USD, so bob can issue 1e16 back and still have
                // 1000 of spendable room left on the same line.
                env(trust(issuer, oppositeLimit));
                env.close();
                env(pay(bob, issuer, bobDebt));
            }
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // Alice deposits 1000 USD, minting 1000 shares 1:1.
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1'000)}));
            env.close();

            // A loan broker on the vault, then a bullet loan at 24% interest:
            // a single payment, one year out.
            auto const brokerKeylet =
                keylet::loanBroker(alice.id(), SeqProxy::rawSequence(env.seq(alice)));
            env(set(alice, vaultKeylet.key));
            env.close();

            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            env(set(carol, brokerKeylet.key, usd(1'000).value()),
                loan::kInterestRate(percentageToTenthBips(24)),
                kGracePeriod(60),
                kPaymentInterval(365 * 24 * 60 * 60),
                kPaymentTotal(1),
                Sig(sfCounterpartySignature, alice),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            // Advance to just before the single payment falls due and let carol
            // repay principal plus interest. LoanPay is what books the accrued
            // interest into sfAssetsTotal; under cash-basis accounting LoanSet
            // alone does not. Share supply stays at 1000, so
            // assetsTotal/sharesTotal becomes 1240/1000.
            env.close(std::chrono::seconds{(365 * 24 * 60 * 60) - 3600});
            env(pay(carol, loanKeylet.key, usd(2'000).value()), Ter(tesSUCCESS));
            env.close();

            // Pin the ratio the rest of the scenario reasons about, so the test cannot quietly
            // stop exercising the bug if the setup drifts.
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault && sleVault->at(sfAssetsTotal) == Number{1'240});
            auto const sleIssuance = env.le(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
            BEAST_EXPECT(sleIssuance && sleIssuance->at(sfOutstandingAmount) == 1'000);

            // Bob deposits 6 USD, which rounds to 10 at his own trust-line
            // scale and so clears the fixCleanup3_2_0 guard. But
            // floor(1000 * 6 / 1240) is 4 shares, worth 4 * 1240 / 1000 = 4.96,
            // and that is below half a ULP of his balance, so it rounds away to
            // nothing when subtracted.
            env(vault.deposit({.depositor = bob, .id = vaultKeylet.key, .amount = usd(6)}),
                Ter(expected));
            env.close();
        };

        // Strip featureLendingProtocolV1_1: this scenario runs an
        // open-ended vault through deposit/broker/loan/repay/deposit,
        // which spans both Subscription and post-loan lifetime — a phase
        // pattern that only makes sense on open-ended vaults. The gate
        // added by LP V1.1 is unrelated to the truncation bug asserted
        // here.
        auto const legacy = testableAmendments() - featureLendingProtocolV1_1;
        {
            testcase(
                "bug: VaultDeposit share truncation lets depositor debit "
                "round away to zero (pre-fixCleanup3_4_0)");
            runScenario(legacy - fixCleanup3_4_0, Line::Holding, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit share truncation lets depositor debit "
                "round away to zero (pre-fixCleanup3_2_0 and pre-fixCleanup3_4_0)");
            runScenario(
                legacy - fixCleanup3_2_0 - fixCleanup3_4_0, Line::Holding, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit share truncation rejected with "
                "tecPRECISION_LOSS (post-fixCleanup3_4_0)");
            runScenario(legacy, Line::Holding, tecPRECISION_LOSS);
        }
        {
            testcase(
                "bug: VaultDeposit share truncation rejected with "
                "tecPRECISION_LOSS (post-fixCleanup3_4_0, pre-fixCleanup3_2_0)");
            runScenario(legacy - fixCleanup3_2_0, Line::Holding, tecPRECISION_LOSS);
        }
        {
            testcase(
                "bug: VaultDeposit share truncation against a debt balance "
                "round away to zero (pre-fixCleanup3_4_0)");
            runScenario(legacy - fixCleanup3_4_0, Line::InDebt, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit share truncation against a debt balance rejected with "
                "tecPRECISION_LOSS (post-fixCleanup3_4_0)");
            runScenario(legacy, Line::InDebt, tecPRECISION_LOSS);
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

    // Shared setup for testBugClawbackRoundTripOvershoot and
    // testBugWithdrawRoundTripOvershoot, which both need a vault at
    // assetsTotal=7, sharesTotal=5 and differ only in what they do once
    // that state is reached.
    //
    // The (7, 5) state is reached through ordinary transactions: a 5 USD
    // deposit mints 5 shares 1:1, then a loan broker on the vault issues a
    // single-payment bullet loan for the full 5 USD at 40% interest. When
    // the borrower repays a year later, LoanPay books the 2 USD of accrued
    // interest into sfAssetsTotal without minting shares, leaving
    // assetsTotal=7 against sharesTotal=5 (see
    // testBugDepositShareTruncationSubUlp for the same technique in more
    // detail).
    struct RoundTripOvershootVault
    {
        test::jtx::Account issuer;
        test::jtx::Account holder;
        PrettyAsset usd;
        test::jtx::Vault vault;
        Keylet vaultKeylet;
        Number initialAssetsTotal;
        Number initialAssetsAvailable;
    };

    std::optional<RoundTripOvershootVault>
    makeRoundTripOvershootVault(test::jtx::Env& env)
    {
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const holder{"holder"};
        Account const borrower{"borrower"};

        env.fund(XRP(10'000), issuer, owner, holder, borrower);
        env.close();

        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const usd = issuer["USD"];
        env.trust(usd(1'000), owner);
        env.trust(usd(1'000), holder);
        env.trust(usd(1'000), borrower);
        env.close();

        env(pay(issuer, holder, usd(100)));
        env(pay(issuer, borrower, usd(100)));
        env.close();

        Vault const vault{env};
        auto [vaultTx, vaultKeylet] = vault.create({.owner = owner, .asset = usd});
        vaultTx[sfScale] = 0;
        env(vaultTx);
        env.close();

        // Holder deposits 5 USD, minting 5 shares 1:1.
        env(vault.deposit({.depositor = holder, .id = vaultKeylet.key, .amount = usd(5)}));
        env.close();

        // A loan broker on the vault, then a single bullet loan for the
        // entire deposit at 40% interest, one payment, one year out.
        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(set(owner, vaultKeylet.key));
        env.close();

        auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
        env(set(borrower, brokerKeylet.key, usd(5).value()),
            loan::kInterestRate(percentageToTenthBips(40)),
            kGracePeriod(60),
            kPaymentInterval(365 * 24 * 60 * 60),
            kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        // Advance to just before the single payment falls due and let the
        // borrower repay principal plus interest. Share supply stays at 5,
        // so assetsTotal/sharesTotal becomes 7/5.
        env.close(std::chrono::seconds{(365 * 24 * 60 * 60) - 3600});
        env(pay(borrower, loanKeylet.key, usd(10).value()), Ter(tesSUCCESS));
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return std::nullopt;
        auto const mptIssuanceID = vaultSle->at(sfShareMPTID);

        Number const initialAssetsTotal = vaultSle->at(sfAssetsTotal);
        Number const initialAssetsAvailable = vaultSle->at(sfAssetsAvailable);
        BEAST_EXPECT(initialAssetsTotal == usd(7).number());
        BEAST_EXPECT(initialAssetsAvailable == usd(7).number());
        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptIssuanceID));
            if (!BEAST_EXPECT(sleIssuance))
                return std::nullopt;
            BEAST_EXPECT(sleIssuance->getFieldU64(sfOutstandingAmount) == 5);
        }

        return RoundTripOvershootVault{
            .issuer = issuer,
            .holder = holder,
            .usd = usd,
            .vault = vault,
            .vaultKeylet = vaultKeylet,
            .initialAssetsTotal = initialAssetsTotal,
            .initialAssetsAvailable = initialAssetsAvailable};
    }

    // VaultClawback::assetsToClawback converts clawbackAmount to shares
    // with round-to-nearest, then round-trips back to assets. When shares
    // round up, assetsRecovered can exceed clawbackAmount.
    //
    // Repro: assetsTotal=7, sharesTotal=5, request 4:
    //   shares = round(20/7) = 3, assets = 7*3/5 = 4.2 > 4.
    //
    // Post-fixCleanup3_4_0: truncate shares so assetsRecovered <=
    // clawbackAmount by construction.
    void
    testBugClawbackRoundTripOvershoot()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, bool withFix) {
            // This regression requires the open-ended vault lifecycle: deposit,
            // originate and repay a loan, then claw back shares. LP V1.1
            // independently rejects attaching a broker to an open-ended vault.
            Env env{*this, features - featureLendingProtocolV1_1};

            auto const setup = makeRoundTripOvershootVault(env);
            if (!BEAST_EXPECT(setup))
                return;

            auto const clawbackAmount = setup->usd(4);
            env(setup->vault.clawback(
                {.issuer = setup->issuer,
                 .id = setup->vaultKeylet.key,
                 .holder = setup->holder,
                 .amount = clawbackAmount.value()}));

            auto const vaultSleAfter = env.current()->read(setup->vaultKeylet);
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            Number const finalAssetsTotal = vaultSleAfter->at(sfAssetsTotal);
            Number const assetsRecovered = setup->initialAssetsTotal - finalAssetsTotal;
            Number const clawbackNum = clawbackAmount.number();

            Number const expectedPost{28LL, -1};
            Number const expectedPre{42LL, -1};
            if (withFix)
            {
                BEAST_EXPECT(assetsRecovered <= clawbackNum);
                BEAST_EXPECT(assetsRecovered == expectedPost);
            }
            else
            {
                BEAST_EXPECT(assetsRecovered > clawbackNum);
                BEAST_EXPECT(assetsRecovered == expectedPre);
            }
        };

        {
            testcase(
                "bug: VaultClawback round-trip overshoot lets issuer recover "
                "more than requested (pre-fixCleanup3_4_0)");
            runScenario(testableAmendments() - fixCleanup3_4_0, false);
        }
        {
            testcase(
                "bug: VaultClawback round-trip overshoot is clamped so "
                "assetsRecovered <= clawbackAmount (post-fixCleanup3_4_0)");
            runScenario(testableAmendments(), true);
        }
    }

    // Same root cause as testBugClawbackRoundTripOvershoot on the
    // withdraw path. Also bypasses the preclaim canWithdraw check, which
    // validates destination limits against the requested amount only.
    //
    // Repro: assetsTotal=7, sharesTotal=5, request 4:
    //   pre-fix : shares = round(20/7) = 3, assets = 7*3/5 = 4.2 > 4.
    //   post-fix: shares = floor(20/7) = 2, assets = 7*2/5 = 2.8 <= 4.
    void
    testBugWithdrawRoundTripOvershoot()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, bool withFix) {
            // This regression requires the open-ended vault lifecycle: deposit,
            // originate and repay a loan, then withdraw shares. LP V1.1
            // independently rejects attaching a broker to an open-ended vault.
            Env env{*this, features - featureLendingProtocolV1_1};

            auto const setup = makeRoundTripOvershootVault(env);
            if (!BEAST_EXPECT(setup))
                return;

            auto const requested = setup->usd(4);
            env(setup->vault.withdraw(
                {.depositor = setup->holder,
                 .id = setup->vaultKeylet.key,
                 .amount = requested.value()}));

            auto const vaultSleAfter = env.current()->read(setup->vaultKeylet);
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            Number const finalAssetsTotal = vaultSleAfter->at(sfAssetsTotal);
            Number const assetsWithdrawn = setup->initialAssetsTotal - finalAssetsTotal;
            Number const requestedNum = requested.number();

            Number const expectedPost{28LL, -1};
            Number const expectedPre{42LL, -1};
            if (withFix)
            {
                BEAST_EXPECT(assetsWithdrawn <= requestedNum);
                BEAST_EXPECT(assetsWithdrawn == expectedPost);
            }
            else
            {
                BEAST_EXPECT(assetsWithdrawn > requestedNum);
                BEAST_EXPECT(assetsWithdrawn == expectedPre);
            }
        };

        {
            testcase(
                "bug: VaultWithdraw round-trip overshoot delivers more than "
                "requested (pre-fixCleanup3_4_0)");
            runScenario(testableAmendments() - fixCleanup3_4_0, false);
        }
        {
            testcase(
                "bug: VaultWithdraw round-trip overshoot is clamped so "
                "assetsWithdrawn <= requested (post-fixCleanup3_4_0)");
            runScenario(testableAmendments(), true);
        }
    }

    void
    testCredentialPinsPseudoAccount()
    {
        using namespace test::jtx;

        // A credential issued to a vault pseudo-account can't be accepted or
        // deleted by it (pseudo-accounts can't sign), so it stays pinned in the
        // pseudo-account's owner directory and blocks VaultDelete with
        // tecHAS_OBLIGATIONS. A pin created before the cure activates is removed
        // by VaultDelete once it does.
        Account const owner{"owner"};
        Account const attacker{"attacker"};
        char const credType[] = "FN36";

        Env env{*this, all_ - fixCleanup3_3_0 - fixCleanup3_4_0};
        env.fund(XRP(1'000'000), owner, attacker);
        env.close();

        Vault const vault{env};
        PrettyAsset const asset = xrpIssue();
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(keylet);
        BEAST_EXPECT(vaultSle);
        Account const pseudo{"vault pseudo-account", vaultSle->at(sfAccount)};
        env.memoize(pseudo);

        // The pseudo-account owns the share issuance; the pin must not change
        // its owner count (an unaccepted credential is owned by the issuer).
        auto const pseudoOwnerCount = ownerCount(env, pseudo);

        testcase("Credential pins vault pseudo-account");
        env(credentials::create(pseudo, attacker, credType));
        env.close();

        auto const credKey = credentials::keylet(pseudo, attacker, credType);
        BEAST_EXPECT(env.le(credKey));
        BEAST_EXPECT(ownerCount(env, attacker) == 1);
        BEAST_EXPECT(ownerCount(env, pseudo) == pseudoOwnerCount);

        // The pin blocks deletion of an otherwise-empty vault.
        env(vault.del({.owner = owner, .id = keylet.key}), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env.enableFeature(fixCleanup3_4_0);
        env.close();

        // The pre-existing pin no longer blocks deletion; the credential is
        // cleaned up and the issuer's owner count is restored.
        testcase("VaultDelete removes pinned credential");
        env(vault.del({.owner = owner, .id = keylet.key}));
        env.close();

        BEAST_EXPECT(!env.le(credKey));
        BEAST_EXPECT(!env.le(keylet));
        BEAST_EXPECT(!env.le(::xrpl::keylet::account(pseudo.id())));
        BEAST_EXPECT(ownerCount(env, attacker) == 0);
    }

    void
    testCredentialPinOverflow()
    {
        using namespace test::jtx;
        testcase("Credential pin cleanup is bounded (tecINCOMPLETE)");

        // A pseudo-account can be pinned with more credentials than one
        // transaction is allowed to clean up. VaultDelete then removes them a
        // bounded batch at a time, returning tecINCOMPLETE until the last batch.
        Account const owner{"owner"};
        Account const attacker{"attacker"};

        Env env{*this, all_ - fixCleanup3_3_0 - fixCleanup3_4_0};
        env.fund(XRP(10'000'000), owner, attacker);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(tx);
        env.close();
        auto const vaultSle = env.le(keylet);
        BEAST_EXPECT(vaultSle);
        Account const pseudo{"vault pseudo-account", vaultSle->at(sfAccount)};
        env.memoize(pseudo);

        // Pin more than one cleanup batch's worth of credentials.
        std::uint16_t const count = kMaxDeletablePseudoAccountCredentials + 3;
        for (std::uint16_t i = 0; i < count; ++i)
            env(credentials::create(pseudo, attacker, std::to_string(i)));
        env.close();
        BEAST_EXPECT(ownerCount(env, attacker) == count);

        env.enableFeature(fixCleanup3_4_0);
        env.close();

        // First delete removes one bounded batch and reports it isn't finished.
        env(vault.del({.owner = owner, .id = keylet.key}), Ter(tecINCOMPLETE));
        env.close();
        BEAST_EXPECT(env.le(keylet));  // vault still exists
        auto const remaining = ownerCount(env, attacker);
        BEAST_EXPECT(remaining > 0 && remaining < count);

        // Second delete finishes the cleanup and removes the vault.
        env(vault.del({.owner = owner, .id = keylet.key}));
        env.close();
        BEAST_EXPECT(!env.le(keylet));
        BEAST_EXPECT(!env.le(::xrpl::keylet::account(pseudo.id())));
        BEAST_EXPECT(ownerCount(env, attacker) == 0);
    }

    struct ImpairedLoanVault
    {
        test::jtx::Account issuer;
        test::jtx::Account holder;
        PrettyAsset usd;
        test::jtx::Vault vault;
        Keylet vaultKeylet;
        MPTID shareId;
    };

    // Impairing a 1,000 loan in a 10,000 vault leaves AssetsAvailable=9,000
    // and AssetsTotal=10,000. otherDeposit > 0 splits the shares, 0 leaves
    // holder as the sole shareholder.
    std::optional<ImpairedLoanVault>
    makeImpairedLoanVault(test::jtx::Env& env, int otherDeposit)
    {
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const holder{"holder"};
        Account const other{"other"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000), issuer, owner, holder, other, borrower);
        env.close();

        env(fset(issuer, asfAllowTrustLineClawback));
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const usd = issuer["USD"];
        env.trust(usd(100'000), owner);
        env.trust(usd(100'000), holder);
        env.trust(usd(100'000), other);
        env.trust(usd(100'000), borrower);
        env.close();

        int const holderDeposit = 10'000 - otherDeposit;
        env(pay(issuer, holder, usd(holderDeposit)));
        if (otherDeposit != 0)
        {
            env(pay(issuer, other, usd(otherDeposit)));
        }
        env.close();

        Vault const vault{env};
        auto const [createTx, vaultKeylet, subscriptionDate] = vault.createClosedEnded(
            {.owner = owner, .asset = usd, .subscriptionOffset = std::chrono::seconds{60}});
        env(createTx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return std::nullopt;
        MPTID const shareId = vaultSle->at(sfShareMPTID);

        env(vault.deposit(
            {.depositor = holder, .id = vaultKeylet.key, .amount = usd(holderDeposit)}));
        if (otherDeposit != 0)
        {
            env(vault.deposit(
                {.depositor = other, .id = vaultKeylet.key, .amount = usd(otherDeposit)}));
        }
        env.close();

        vault.closePastSubscription(subscriptionDate);

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(set(owner, vaultKeylet.key));
        env.close();

        auto const sleBroker = env.le(brokerKeylet);
        if (!BEAST_EXPECT(sleBroker))
            return std::nullopt;
        auto const loanKeylet =
            keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        env(set(borrower, brokerKeylet.key, usd(1'000).value()),
            loan::kInterestRate(percentageToTenthBips(0)),
            kGracePeriod(60),
            kPaymentInterval(120),
            kPaymentTotal(10),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        // Under fixCleanup3_4_0, LoanManage rejects tfLoanImpair with
        // tecTOO_SOON unless the payment is already late; advance the ledger
        // past sfNextPaymentDueDate so impairment succeeds. No-op otherwise.
        if (env.current()->rules().enabled(fixCleanup3_4_0))
        {
            auto const loanBefore = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanBefore))
                return std::nullopt;
            std::uint32_t const dueDate = loanBefore->at(sfNextPaymentDueDate);
            env.close(NetClock::time_point{NetClock::duration{dueDate}} + std::chrono::seconds{1});
        }

        env(manage(owner, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultAfter))
            return std::nullopt;
        BEAST_EXPECT(vaultAfter->at(sfAssetsAvailable) == usd(9'000).value());
        BEAST_EXPECT(vaultAfter->at(sfLossUnrealized) == usd(1'000).value());

        return ImpairedLoanVault{
            .issuer = issuer,
            .holder = holder,
            .usd = usd,
            .vault = vault,
            .vaultKeylet = vaultKeylet,
            .shareId = shareId};
    }

    // Legacy clawback pricing burns every share; fixCleanup3_4_0 leaves 10%
    // outstanding, backed by the impaired receivable.
    void
    testBugClawbackAfterLoanImpair()
    {
        using namespace test::jtx;

        auto clawbackHolder = [](ImpairedLoanVault const& setup, STAmount const& amount) {
            return setup.vault.clawback(
                {.issuer = setup.issuer,
                 .id = setup.vaultKeylet.key,
                 .holder = setup.holder,
                 .amount = amount});
        };

        auto runSole = [this, &clawbackHolder](FeatureBitset features, TER expected) {
            testcase(
                features[fixCleanup3_4_0]
                    ? "VaultClawback after impaired loan (post-fixCleanup3_4_0)"
                    : "VaultClawback after impaired loan (pre-fixCleanup3_4_0)");

            Env env(*this, features);
            auto const maybeSetup = makeImpairedLoanVault(env, 0);
            if (!maybeSetup)
            {
                BEAST_EXPECT(false);
                return;
            }
            ImpairedLoanVault const& setup = *maybeSetup;

            auto const tokenBefore = env.le(keylet::mptoken(setup.shareId, setup.holder.id()));
            auto const vaultBefore = env.le(setup.vaultKeylet);
            auto const issuanceBefore = env.le(keylet::mptokenIssuance(setup.shareId));
            if (!BEAST_EXPECT(tokenBefore) || !BEAST_EXPECT(vaultBefore) ||
                !BEAST_EXPECT(issuanceBefore))
                return;
            std::uint64_t const sharesBefore = tokenBefore->getFieldU64(sfMPTAmount);

            // The clawback of 19,000 exceeds AssetsAvailable (9,000), so
            // VaultClawback clamps sharesDestroyed to whatever redeems
            // exactly AssetsAvailable; compute that expected value using the
            // same conversion helper VaultClawback itself uses, rather than
            // assuming an exact 90/10 split holds under truncation.
            auto const maybeSharesDestroyed = assetsToSharesWithdraw(
                vaultBefore,
                issuanceBefore,
                setup.usd(9'000).value(),
                TruncateShares::Yes,
                WaiveUnrealizedLoss::Yes);
            if (!BEAST_EXPECT(maybeSharesDestroyed))
                return;
            std::uint64_t const expectedSharesAfter =
                sharesBefore - maybeSharesDestroyed->mpt().value();

            env(clawbackHolder(setup, setup.usd(19'000).value()), Ter(expected));
            env.close();
            if (expected != tesSUCCESS)
                return;

            auto const vaultAfter = env.le(setup.vaultKeylet);
            if (!BEAST_EXPECT(vaultAfter))
                return;
            BEAST_EXPECT(vaultAfter->at(sfAssetsAvailable) == setup.usd(0).value());
            BEAST_EXPECT(vaultAfter->at(sfAssetsTotal) == setup.usd(1'000).value());
            BEAST_EXPECT(vaultAfter->at(sfLossUnrealized) == setup.usd(1'000).value());
            auto const tokenAfter = env.le(keylet::mptoken(setup.shareId, setup.holder.id()));
            if (!BEAST_EXPECT(tokenAfter))
                return;
            BEAST_EXPECT(tokenAfter->getFieldU64(sfMPTAmount) == expectedSharesAfter);
        };

        runSole(all_ - fixCleanup3_4_0, tecINVARIANT_FAILED);
        runSole(all_, tesSUCCESS);

        testcase("VaultClawback after impaired loan, non-sole holder");
        {
            Env env(*this, all_);
            auto const maybeSetup = makeImpairedLoanVault(env, 1'000);
            if (!maybeSetup)
            {
                BEAST_EXPECT(false);
                return;
            }
            ImpairedLoanVault const& setup = *maybeSetup;
            // The waiver does not apply, so the holder's 9,000 shares are
            // still priced at the discounted rate and cannot cover 9,000.
            env(clawbackHolder(setup, setup.usd(9'000).value()), Ter(tecINSUFFICIENT_FUNDS));
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
        testBugDepositShareTruncationSubUlp();
        testVaultWithdrawCanonicalizeToZero();
        testBugVaultDustDebitCanonicalizesToNoOp();
        testVaultDepositNegativeBalanceFromOppositeLimit();
        testCredentialPinsPseudoAccount();
        testCredentialPinOverflow();
        testBug6LimitBypassWithShares();
        testBugClawbackRoundTripOvershoot();
        testBugWithdrawRoundTripOvershoot();
        testBugClawbackAfterLoanImpair();
    }
};

BEAST_DEFINE_TESTSUITE(VaultBugs, app, xrpl);

}  // namespace xrpl
