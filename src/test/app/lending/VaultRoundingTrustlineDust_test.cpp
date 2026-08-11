#include <test/app/lending/LoanTestBase.h>
#include <test/app/lending/VaultDustProbe.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <optional>
#include <string>

// ============================================================================
// VaultRoundingTrustlineDust_test.cpp — solution B'-specific tests
// (docs/plan-vault-dust-b-prime-field-accounting-kept.md §13). This file is
// NOT shared: it is free to reference sfDust, DustSplit, useVaultDust, and
// friends directly, unlike src/test/app/lending/VaultRounding_test.cpp.
// ============================================================================

namespace xrpl::test {

class VaultRoundingTrustlineDust_test : public LoanTestBase
{
private:
    struct DustFixture
    {
        jtx::Account issuer;
        jtx::Account lender;
        jtx::Account borrower;
        jtx::PrettyAsset asset;
        BrokerInfo broker;
        Keylet tinyLoanKeylet;
    };

    // Same recipe as VaultRounding_test.cpp's withDustSetup (testsuite doc
    // §5), parameterized by owner-account NAME so callers can search for
    // both trust-line sign orientations (plan §13.2): the vault
    // pseudo-account's address is a hash that depends on the owner and
    // ledger state, so varying the owner varies which side of the issuer
    // the pseudo-account lands on.
    std::optional<DustFixture>
    makeDustFixture(jtx::Env& env, std::string const& ownerSuffix)
    {
        using namespace jtx;
        using namespace loan;

        Account const issuer{"issuer" + ownerSuffix};
        Account const lender{"lender" + ownerSuffix};
        Account const borrower{"borrower" + ownerSuffix};
        env.fund(XRP(1'000'000'00), issuer, lender, borrower);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(50'000)));
        env(trust(borrower, asset(50'000)));
        env.close();
        env(pay(issuer, lender, asset(30'000)));
        env(pay(issuer, borrower, asset(1'000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{13'370},
            .coverDeposit = 5'000,
            .managementFeeRate = TenthBips16{0}};

        BrokerInfo const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        auto const brokerSle1 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle1))
            return std::nullopt;
        Keylet const tinyLoanKeylet = keylet::loan(broker.brokerID, brokerSle1->at(sfLoanSequence));

        env(set(borrower, broker.brokerID, Number{1, -2}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{1'922}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        Vault const vault{env};
        env(vault.deposit(
            {.depositor = lender, .id = broker.vaultKeylet().key, .amount = asset(9'500)}));
        env.close();

        auto const tinyLoanSle = env.le(tinyLoanKeylet);
        auto const vaultSle = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(tinyLoanSle) || !BEAST_EXPECT(vaultSle))
            return std::nullopt;
        if (tinyLoanSle->at(sfLoanScale) != -12 || getVaultScale(vaultSle) != -11)
        {
            log << "VaultRoundingTrustlineDust: fixture did not reproduce -12/-11 "
                   "for owner suffix '"
                << ownerSuffix << "'" << std::endl;
            return std::nullopt;
        }

        return DustFixture{
            .issuer = issuer,
            .lender = lender,
            .borrower = borrower,
            .asset = asset,
            .broker = broker,
            .tinyLoanKeylet = tinyLoanKeylet};
    }

    void
    payTinyLoanInFull(jtx::Env& env, DustFixture const& fx)
    {
        using namespace jtx;
        auto const loanSle = env.le(fx.tinyLoanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        auto const periodicPayment = loanSle->at(sfPeriodicPayment);
        auto const serviceFee = loanSle->at(sfLoanServiceFee);
        std::int32_t const loanScale = loanSle->at(sfLoanScale);
        auto const payment = roundPeriodicPayment(fx.asset.raw(), periodicPayment, loanScale);
        auto const payAmt = STAmount{fx.asset.raw(), payment + serviceFee};
        env(jtx::loan::pay(fx.borrower, fx.tinyLoanKeylet.key, payAmt), Fee(XRP(10)));
        env.close();
    }

    //--------------------------------------------------------------------
    // §13.1 The field
    //--------------------------------------------------------------------

    void
    testDustAbsentReadsZero(FeatureBitset features)
    {
        testcase("sfDust absent on an untouched trust line reads as zero");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1'000), alice, bob);
        env.close();
        PrettyAsset const asset = alice["USD"];
        env(trust(bob, asset(1'000)));
        env.close();

        auto const line =
            env.le(keylet::trustLine(alice.id(), bob.id(), asset.raw().get<Issue>().currency));
        if (!BEAST_EXPECT(line))
            return;
        BEAST_EXPECT(!line->isFieldPresent(sfDust));
        BEAST_EXPECT(Number{line->at(sfDust)} == beast::kZero);
    }

    void
    testNoDustForLegacyOrIntegralVaults(FeatureBitset features)
    {
        testcase("Legacy and integral-asset vaults never carry sfDust");

        using namespace jtx;
        Env env{*this, features};  // featureLendingProtocolV1_1 excluded => Legacy
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        env.fund(XRP(1'000'000), issuer, lender);
        env.close();
        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(500'000)));
        env.close();
        env(pay(issuer, lender, asset(400'000)));
        env.close();

        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::Legacy);
        BEAST_EXPECT(!vault_dust::useVaultDust(*env.current(), vaultSle));
        BEAST_EXPECT(readVaultDust(env, vaultKeylet) == beast::kZero);
    }

    //--------------------------------------------------------------------
    // §13.2 Sign convention — both orientations, explicitly forced.
    //--------------------------------------------------------------------

    void
    testDustBothSignOrientations(FeatureBitset features)
    {
        testcase("Dust lifecycle holds with the Vault as both low and high account");

        using namespace jtx;

        bool sawLow = false, sawHigh = false;
        for (int attempt = 0; attempt < 12 && !(sawLow && sawHigh); ++attempt)
        {
            Env env{*this, features | featureLendingProtocolV1_1};
            auto const fx = makeDustFixture(env, std::to_string(attempt));
            if (!fx)
                continue;

            auto const vaultSle = env.le(fx->broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSle))
                continue;
            AccountID const vaultAccount = vaultSle->at(sfAccount);
            AccountID const issuerAccount = fx->issuer.id();
            bool const vaultIsHigh = vaultAccount > issuerAccount;

            if (vaultIsHigh && sawHigh)
                continue;
            if (!vaultIsHigh && sawLow)
                continue;

            payTinyLoanInFull(env, *fx);

            Number const dust = readVaultDust(env, fx->broker.vaultKeylet());
            if (dust == beast::kZero)
                continue;  // this attempt didn't generate dust; try another

            // Read the raw ledger field and confirm the probe undid the
            // low/high convention correctly: the raw sfDust value's sign
            // (in the line's own convention, positive = low account holds
            // high account's IOUs) must match vaultIsHigh appropriately.
            auto const line = env.le(
                keylet::trustLine(
                    vaultAccount, issuerAccount, fx->asset.raw().get<Issue>().currency));
            if (!BEAST_EXPECT(line))
                continue;
            Number const rawDust = line->at(sfDust);

            if (vaultIsHigh)
            {
                sawHigh = true;
                // Vault-positive dust (dust > 0, meaning the Vault is
                // carrying unrecognized value) corresponds to a NEGATIVE
                // raw field when the Vault is the high account, since the
                // raw convention is "low account's terms".
                BEAST_EXPECT((dust > beast::kZero) == (rawDust < beast::kZero));
            }
            else
            {
                sawLow = true;
                BEAST_EXPECT((dust > beast::kZero) == (rawDust > beast::kZero));
            }

            // The mirror still holds regardless of orientation (O1).
            Number const avail = vaultSle->at(sfAssetsAvailable);
            (void)avail;
            auto const vaultSleAfter = env.le(fx->broker.vaultKeylet());
            if (BEAST_EXPECT(vaultSleAfter))
            {
                Number const a = vaultSleAfter->at(sfAssetsAvailable);
                Number const b = accountHolds(
                    *env.current(),
                    vaultSleAfter->at(sfAccount),
                    fx->asset.raw(),
                    FreezeHandling::IgnoreFreeze,
                    AuthHandling::IgnoreAuth,
                    beast::Journal{beast::Journal::getNullSink()});
                BEAST_EXPECT(a == b);
            }
        }

        BEAST_EXPECT(sawLow);
        BEAST_EXPECT(sawHigh);
    }

    //--------------------------------------------------------------------
    // §13.3 The credit path (driven through real transactions)
    //--------------------------------------------------------------------

    void
    testDustCreatedAndPromoted(FeatureBitset features)
    {
        testcase("Credit path: split with remainder, and promotion on accumulation");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "promote");
        if (!fx)
            return;

        Number const dustBefore = readVaultDust(env, fx->broker.vaultKeylet());
        BEAST_EXPECT(dustBefore == beast::kZero);

        payTinyLoanInFull(env, *fx);

        Number const dustAfter = readVaultDust(env, fx->broker.vaultKeylet());
        // The fixture is specifically built so this repayment carries digits
        // finer than the vault's scale (testsuite doc §5) — dust MUST be
        // non-zero, or the whole fixture is vacuous.
        BEAST_EXPECT(dustAfter != beast::kZero);
        BEAST_EXPECT(dustAfter > beast::kZero);

        auto const vaultSle = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSle))
            return;
        Number const q{1, getVaultScale(vaultSle)};
        BEAST_EXPECT(dustAfter < q);
    }

    void
    testNullptrPathUnchanged(FeatureBitset features)
    {
        testcase("Ordinary payments never acquire sfDust (nullptr path)");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), alice, bob, carol);
        env.close();
        PrettyAsset const asset = alice["USD"];
        env(trust(bob, asset(10'000)));
        env(trust(carol, asset(10'000)));
        env.close();
        env(pay(alice, bob, asset(1'000)));
        env.close();
        env(pay(bob, carol, asset(Number{1, -7})));
        env.close();

        // carol trusts the ISSUER (alice), not bob directly, so bob's
        // payment to carol ripples through alice — the two lines actually
        // touched are (alice,bob) and (alice,carol), not (bob,carol).
        auto const lineAB =
            env.le(keylet::trustLine(alice.id(), bob.id(), asset.raw().get<Issue>().currency));
        auto const lineAC =
            env.le(keylet::trustLine(alice.id(), carol.id(), asset.raw().get<Issue>().currency));
        if (BEAST_EXPECT(lineAB))
            BEAST_EXPECT(
                !lineAB->isFieldPresent(sfDust) || Number{lineAB->at(sfDust)} == beast::kZero);
        if (BEAST_EXPECT(lineAC))
            BEAST_EXPECT(
                !lineAC->isFieldPresent(sfDust) || Number{lineAC->at(sfDust)} == beast::kZero);
    }

    //--------------------------------------------------------------------
    // §13.5 Lifecycle guards
    //--------------------------------------------------------------------

    void
    testAccountHoldsExcludesDust(FeatureBitset features)
    {
        testcase("accountHolds returns sfBalance alone, never sfBalance + sfDust");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "acctholds");
        if (!fx)
            return;

        payTinyLoanInFull(env, *fx);

        Number const dust = readVaultDust(env, fx->broker.vaultKeylet());
        if (dust == beast::kZero)
            return;  // fixture failed to generate dust this run; nothing to check

        auto const vaultSle = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSle))
            return;

        Number const holds = accountHolds(
            *env.current(),
            vaultSle->at(sfAccount),
            fx->asset.raw(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            beast::Journal{beast::Journal::getNullSink()});
        Number const assetsAvailable = vaultSle->at(sfAssetsAvailable);

        // If accountHolds leaked sfDust into its result, holds would exceed
        // assetsAvailable by roughly `dust`. It must not.
        BEAST_EXPECT(holds == assetsAvailable);
    }

    void
    testVaultDeleteRequiresZeroDust(FeatureBitset features)
    {
        testcase("VaultDelete is blocked while sfDust is non-zero, and succeeds once it is zero");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};

        Account const issuer{"issuer2"};
        Account const lender{"lender2"};
        env.fund(XRP(1'000'000), issuer, lender);
        env.close();
        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(500'000)));
        env.close();
        env(pay(issuer, lender, asset(400'000)));
        env.close();

        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const vaultSleBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        STAmount const allAssets{asset.raw(), vaultSleBefore->at(sfAssetsAvailable)};

        env(vault.withdraw({.depositor = lender, .id = vaultKeylet.key, .amount = allAssets}));
        env.close();

        BEAST_EXPECT(readVaultDust(env, vaultKeylet) == beast::kZero);

        env(vault.del({.owner = lender, .id = vaultKeylet.key}));
        env.close();

        BEAST_EXPECT(!env.le(vaultKeylet));
    }

    //--------------------------------------------------------------------
    // §13.6 Re-normalisation
    //--------------------------------------------------------------------

    // Exercise the interaction between a dust-producing repayment and a
    // subsequent NON-terminal VaultWithdraw. Historically this combination
    // tripped
    //   "withdrawal must change vault and destination balance by equal
    //   amount"
    // in ValidVault (src/libxrpl/tx/invariants/VaultInvariant.cpp) when the
    // withdrawal drove the Vault's posterior scale finer than the custody
    // line's, allowing renormaliseStrandedDust to promote whole quanta
    // from sfDust into sfBalance. That promotion is a pure recognition
    // move (no external cash flow), so comparing sfBalance-only deltas
    // between the pseudo (which sees the promotion) and the destination
    // (which does not) mis-attributed a quantum-worth of drift to the
    // wrong side. The fix compares each side's EXTENDED balance
    // (sfBalance + sfDust) — see the "extended balance" branch of the
    // destination check in @c ttVAULT_WITHDRAW.
    //
    // Under the current implementation the non-terminal branch mutates the
    // Vault SLE and the pseudo-account's custody line by the same amount,
    // and any renormalisation adds identical whole-quantum deltas to both
    // T and A. The ValidVault destination check now uses extended balance
    // and therefore holds even when M > 0.
    void
    testNonTerminalWithdrawAfterDust(FeatureBitset features)
    {
        testcase(
            "Non-terminal VaultWithdraw after a dust-producing repayment satisfies ValidVault");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "nontermwd");
        if (!fx)
            return;

        payTinyLoanInFull(env, *fx);

        Number const dustAfterRepay = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustAfterRepay == beast::kZero)
            return;  // fixture did not generate dust this run

        auto const vaultSleAfterRepay = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterRepay))
            return;
        Number const availAfterRepay = vaultSleAfterRepay->at(sfAssetsAvailable);
        Number const totalAfterRepay = vaultSleAfterRepay->at(sfAssetsTotal);

        // Partial (non-terminal) withdrawal — take a chunk large enough
        // to refine the Vault's posterior scale (10500 -> ~2), which is
        // what triggers renormaliseStrandedDust to promote whole quanta
        // out of the custody line's sfDust. Values chosen to leave
        // outstanding shares (avoiding the terminal branch).
        Vault const vault{env};
        STAmount const withdrawAmount{fx->asset.raw(), Number{10'498}};
        env(vault.withdraw(
            {.depositor = fx->lender,
             .id = fx->broker.vaultKeylet().key,
             .amount = withdrawAmount}));
        env.close();

        // If ValidVault trips, the transaction is not committed — a
        // successful commit is the primary oracle. But also cross-check
        // the surviving invariant explicitly.
        auto const vaultSleAfterWithdraw = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterWithdraw))
            return;

        Number const availAfterWd = vaultSleAfterWithdraw->at(sfAssetsAvailable);
        Number const totalAfterWd = vaultSleAfterWithdraw->at(sfAssetsTotal);

        // Delta(sfAssetsTotal) must equal Delta(sfAssetsAvailable) —
        // non-terminal removal subtracts `amount` from both, and any
        // renormalisation adds the same movable delta to both.
        BEAST_EXPECT((totalAfterWd - totalAfterRepay) == (availAfterWd - availAfterRepay));

        // Dust must still be strictly less than one quantum at the new
        // scale (O2).
        Number const dustAfterWd = readVaultDust(env, fx->broker.vaultKeylet());
        Number const q{1, getVaultScale(vaultSleAfterWithdraw)};
        BEAST_EXPECT(dustAfterWd >= beast::kZero);
        BEAST_EXPECT(dustAfterWd < q);
    }

    // Companion: another non-terminal step (a second partial withdraw)
    // exercises the case where sfDust is non-zero going *into* the
    // withdraw, potentially getting promoted by renormalisation. Same
    // invariant contract: the ValidVault check must hold.
    void
    testSecondNonTerminalWithdrawAfterDust(FeatureBitset features)
    {
        testcase(
            "Two successive non-terminal VaultWithdraws after dust-producing repayment satisfy "
            "ValidVault");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "twowd");
        if (!fx)
            return;

        payTinyLoanInFull(env, *fx);

        if (readVaultDust(env, fx->broker.vaultKeylet()) == beast::kZero)
            return;

        Vault const vault{env};
        STAmount const firstWd{fx->asset.raw(), Number{5'000}};
        env(vault.withdraw(
            {.depositor = fx->lender, .id = fx->broker.vaultKeylet().key, .amount = firstWd}));
        env.close();

        auto const vaultSleMid = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleMid))
            return;
        Number const availMid = vaultSleMid->at(sfAssetsAvailable);
        Number const totalMid = vaultSleMid->at(sfAssetsTotal);

        STAmount const secondWd{fx->asset.raw(), Number{5'490}};
        env(vault.withdraw(
            {.depositor = fx->lender, .id = fx->broker.vaultKeylet().key, .amount = secondWd}));
        env.close();

        auto const vaultSleEnd = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleEnd))
            return;

        Number const availEnd = vaultSleEnd->at(sfAssetsAvailable);
        Number const totalEnd = vaultSleEnd->at(sfAssetsTotal);

        BEAST_EXPECT((totalEnd - totalMid) == (availEnd - availMid));

        Number const dustEnd = readVaultDust(env, fx->broker.vaultKeylet());
        Number const q{1, getVaultScale(vaultSleEnd)};
        BEAST_EXPECT(dustEnd >= beast::kZero);
        BEAST_EXPECT(dustEnd < q);
    }

    //--------------------------------------------------------------------
    // Two-leg refactor tests (§ Two-leg DustSplit refactor)
    //
    // These exercise the plan's new sender-leg policies (Override on
    // clawback / non-terminal withdrawal / move, Drain on terminal
    // withdrawal) end-to-end via real transactors, so they cover both
    // the trust-line layer's per-leg mechanics and the Vault-side
    // reconciliation through split.sender->dustDelta.
    //--------------------------------------------------------------------

    // Sender-leg Override renormalisation: a scale-refining withdrawal
    // promotes stranded dust from sfDust back into sfBalance, and the
    // Vault's sfAssetsTotal / sfAssetsAvailable both grow by the
    // promoted amount so the receivable invariant is preserved.
    void
    testSenderLegOverrideNonTerminalPromotes(FeatureBitset features)
    {
        testcase("Sender-leg Override renormalises stranded dust on non-terminal withdraw");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "senderoverride");
        if (!fx)
            return;

        // Generate dust via the tiny loan repayment.
        payTinyLoanInFull(env, *fx);

        Number const dustAfterRepay = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustAfterRepay == beast::kZero)
            return;  // fixture didn't generate dust this run

        auto const vaultSleAfterRepay = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterRepay))
            return;
        int const scaleBefore = getVaultScale(vaultSleAfterRepay);
        Number const totalBefore = vaultSleAfterRepay->at(sfAssetsTotal);
        Number const availBefore = vaultSleAfterRepay->at(sfAssetsAvailable);

        // Large partial withdrawal — refines the posterior scale so
        // the dust reservoir crosses a decade boundary and gets
        // promoted by the sender-leg Override re-split.
        Vault const vault{env};
        STAmount const withdrawAmount{fx->asset.raw(), Number{10'498}};
        env(vault.withdraw(
            {.depositor = fx->lender,
             .id = fx->broker.vaultKeylet().key,
             .amount = withdrawAmount}));
        env.close();

        auto const vaultSleAfterWd = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterWd))
            return;
        int const scaleAfter = getVaultScale(vaultSleAfterWd);
        Number const dustAfterWd = readVaultDust(env, fx->broker.vaultKeylet());

        // The withdrawal was sized to refine the scale.
        BEAST_EXPECT(scaleAfter < scaleBefore);

        // The Vault's T and A both moved by the SAME extended delta —
        // exactly the invariant the sender-leg Override reconciliation
        // preserves (both fields shift by `-amount` plus the promoted
        // dust). If Override's reconciliation were miscoded, T and A
        // would diverge.
        Number const totalAfter = vaultSleAfterWd->at(sfAssetsTotal);
        Number const availAfter = vaultSleAfterWd->at(sfAssetsAvailable);
        BEAST_EXPECT((totalAfter - totalBefore) == (availAfter - availBefore));

        // The remaining dust on the custody line is bounded by one
        // quantum at the new posterior scale (up to a decade of
        // Override drift, which the O2 relaxation tolerates).
        Number const bound{10, scaleAfter};
        BEAST_EXPECT(dustAfterWd >= beast::kZero);
        BEAST_EXPECT(dustAfterWd < bound);
    }

    // Sender-leg Drain end-to-end: a terminal removal empties the
    // custody line's reservoir into the destination. Vault code does
    // no manual sfDust write; the trust-line layer folds sfDust into
    // sfBalance, adjusts the outgoing amount, and zeroes sfDust. Post-
    // condition: line has sfBalance == 0 and sfDust == 0; the Vault SLE
    // has sfAssetsTotal == 0 and sfAssetsAvailable == 0.
    void
    testSenderLegDrainTerminalRemoval(FeatureBitset features)
    {
        testcase("Sender-leg Drain drains reservoir end-to-end on terminal removal");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "senderdrain");
        if (!fx)
            return;

        // Seed a non-zero reservoir on the custody line by paying the
        // first periodic instalment (returns interest with sub-quantum
        // residual).
        payTinyLoanInFull(env, *fx);

        Number const dustBeforeTerminal = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustBeforeTerminal == beast::kZero)
            return;  // fixture didn't produce dust this run
        BEAST_EXPECT(dustBeforeTerminal > beast::kZero);

        // The remainder of the tiny loan is outstanding, which keeps
        // sfAssetsAvailable < sfAssetsTotal and blocks a terminal
        // withdrawal (the lender's shares still back the outstanding
        // principal). Advance time past the next payment due date +
        // grace window (loan is 2 payments at 1-year intervals; one
        // has been made) and default the loan so no principal is
        // outstanding and the vault SLE's sfAssetsTotal drops to
        // sfAssetsAvailable.
        env.close(std::chrono::seconds(86400 * 800));
        env(jtx::loan::manage(fx->lender, fx->tinyLoanKeylet.key, tfLoanDefault));
        env.close();

        auto const vaultSleBefore = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleBefore))
            return;

        Number const dustAfterDefault = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustAfterDefault == beast::kZero)
            return;  // default consumed the reservoir; nothing left to test

        Number const lenderBalanceBefore = env.balance(fx->lender, fx->asset).number();
        Number const availBefore = Number{vaultSleBefore->at(sfAssetsAvailable)};

        // Withdraw the full available balance in one shot. With the
        // loan defaulted, sfAssetsAvailable == sfAssetsTotal and the
        // lender holds all outstanding shares, so this burns every
        // share and triggers FinalRemoval::Yes inside VaultWithdraw —
        // the path that installs the Drain policy on the sender leg.
        STAmount const allAssets{fx->asset.raw(), vaultSleBefore->at(sfAssetsAvailable)};
        Vault const vault{env};
        env(vault.withdraw(
            {.depositor = fx->lender, .id = fx->broker.vaultKeylet().key, .amount = allAssets}));
        env.close();

        auto const vaultSleAfter = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfter))
            return;

        // Vault SLE ends terminal: both totals reset to zero.
        BEAST_EXPECT(Number{vaultSleAfter->at(sfAssetsAvailable)} == beast::kZero);
        BEAST_EXPECT(Number{vaultSleAfter->at(sfAssetsTotal)} == beast::kZero);

        // Custody line's sfDust must be zero — Drain zeroed it inside
        // the trust-line layer, not via a manual fold in Vault code.
        Number const dustAfter = readVaultDust(env, fx->broker.vaultKeylet());
        BEAST_EXPECT(dustAfter == beast::kZero);

        // The destination (lender) received at least the vault's
        // pre-terminal sfAssetsAvailable plus the reservoir that was
        // drained — the observable end-to-end effect of Drain mode:
        // value that used to be stranded in sfDust reaches the
        // destination in the same transaction.
        Number const lenderBalanceAfter = env.balance(fx->lender, fx->asset).number();
        Number const received = lenderBalanceAfter - lenderBalanceBefore;
        BEAST_EXPECT(received >= availBefore);
        BEAST_EXPECT(received >= availBefore + dustAfterDefault);
    }

public:
    void
    run() override
    {
        testDustAbsentReadsZero(all_);
        testNoDustForLegacyOrIntegralVaults(all_);
        testDustBothSignOrientations(all_);
        testDustCreatedAndPromoted(all_);
        testNullptrPathUnchanged(all_);
        testAccountHoldsExcludesDust(all_);
        testVaultDeleteRequiresZeroDust(all_);
        testNonTerminalWithdrawAfterDust(all_);
        testSecondNonTerminalWithdrawAfterDust(all_);
        testSenderLegOverrideNonTerminalPromotes(all_);
        testSenderLegDrainTerminalRemoval(all_);
    }
};

BEAST_DEFINE_TESTSUITE(VaultRoundingTrustlineDust, tx, xrpl);

}  // namespace xrpl::test
