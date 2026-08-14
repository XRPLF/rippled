#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

#include <stdexcept>
#include <string>
#include <utility>

// Arithmetic tests for the DustSplit primitive. Complements
// DustSplitCreditPath_test.cpp, which pins the nullptr-policy contract
// but does not exercise Override or Drain math at all.
//
// Every test drives directSendNoFeeIOU / directSendNoLimitIOU indirectly
// through accountSend or accountSendMulti. All observations happen via
// creditBalanceExact and direct sfBalance / sfDust reads on the trust
// line. No consumer of DustSplit exists on this branch — these tests
// construct DustSplit values locally in a Sandbox that never persists.
//
// Sign convention. The trust line stores sfBalance and sfDust in the
// low-account's own positive convention (positive value on either field
// means the low account is the creditor / holds the reservoir). The
// DustSplit out-fields balanceDelta / dustDelta are reported in the
// leg's non-issuer party's terms. Every helper below keeps a strict
// distinction between "ledger terms" and "party terms".

namespace xrpl::test {

namespace {

// ----------------------------------------------------------------------
// Sign-convention helpers. Convert between "ledger terms" (low-account
// positive) and "party terms" (positive from a chosen party's view).
// ----------------------------------------------------------------------

[[nodiscard]] inline bool
partyIsHigh(AccountID const& party, AccountID const& issuer)
{
    return party > issuer;
}

[[nodiscard]] inline Number
toPartyTerms(Number ledger, AccountID const& party, AccountID const& issuer)
{
    return partyIsHigh(party, issuer) ? -ledger : ledger;
}

[[nodiscard]] inline Number
toLedgerTerms(Number party, AccountID const& holder, AccountID const& issuer)
{
    return partyIsHigh(holder, issuer) ? -party : party;
}

[[nodiscard]] STAmount
readBalancePartyTerms(SLE const& line, AccountID const& holder, AccountID const& issuer)
{
    STAmount b = line.getFieldAmount(sfBalance);
    if (partyIsHigh(holder, issuer))
        b.negate();
    return b;
}

[[nodiscard]] Number
readDustPartyTerms(SLE const& line, AccountID const& holder, AccountID const& issuer)
{
    return toPartyTerms(Number{line.at(sfDust)}, holder, issuer);
}

// ----------------------------------------------------------------------
// LegPolicy factories. Every test uses one of these two shapes.
// ----------------------------------------------------------------------

[[nodiscard]] inline DustSplit::LegPolicy
overrideLeg(int scale)
{
    return DustSplit::LegPolicy{
        .mode = DustSplit::LegPolicy::Mode::Override, .overrideScale = scale};
}

[[nodiscard]] inline DustSplit::LegPolicy
drainLeg()
{
    return DustSplit::LegPolicy{.mode = DustSplit::LegPolicy::Mode::Drain};
}

// Wraps accountSend with the default (non-MPT, non-waived, no-sponsor)
// flags used by every test in this suite.
[[nodiscard]] TER
sendVia(
    Sandbox& sb,
    beast::Journal j,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    DustSplit* d = nullptr)
{
    return accountSend(
        sb,
        from,
        to,
        amount,
        j,
        /*sponsorSle=*/{},
        WaiveTransferFee::No,
        AllowMPTOverflow::No,
        d);
}

// Seed sfBalance and sfDust on an already-created line, given values
// expressed in the holder's own terms.
void
seedLine(
    Sandbox& sb,
    AccountID const& holder,
    Issue const& iss,
    STAmount balancePartyTerms,
    Number const& dustPartyTerms)
{
    auto line = sb.peek(keylet::trustLine(holder, iss));
    if (!line)
        Throw<std::runtime_error>("seedLine: trust line missing");
    if (partyIsHigh(holder, iss.account))
        balancePartyTerms.negate();
    balancePartyTerms.get<Issue>().account = noAccount();
    line->setFieldAmount(sfBalance, balancePartyTerms);
    line->at(sfDust) = toLedgerTerms(dustPartyTerms, holder, iss.account);
    sb.update(line);
}

// Common preamble: fund and open a trust line, seed initial balance via a
// real payment so all reserve/limit/flag state is naturally set up (which
// matters for auto-delete tests).
void
setupHolderLine(
    jtx::Env& env,
    jtx::Account const& issuer,
    jtx::Account const& holder,
    jtx::PrettyAsset const& asset,
    STAmount const& initialHolderBalance,
    STAmount const& trustLimit)
{
    using namespace jtx;
    env.fund(XRP(10'000), issuer, holder);
    env.close();
    env(trust(holder, trustLimit));
    env.close();
    if (initialHolderBalance != beast::kZero)
    {
        env(pay(issuer, holder, initialHolderBalance));
        env.close();
    }
}

}  // namespace

class DustSplitArithmetic_test : public beast::unit_test::Suite
{
    FeatureBitset const all_{jtx::testableAmendments()};
    FeatureBitset const withDust_{all_ | featureLendingProtocolV1_1};

    // ------------------------------------------------------------------
    // Sandbox harness
    // ------------------------------------------------------------------

    // Runs @p fn inside a throwaway Sandbox opened on the current open
    // ledger. The sandbox is never applied (`modify` returns false), so
    // tests are hermetic w.r.t. one another and can freely poke sfDust
    // via seedLine.
    template <typename Fn>
    void
    withSandbox(jtx::Env& env, Fn&& fn)
    {
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            Sandbox sb(&view, TapNone);
            fn(sb, j);
            return false;
        });
    }

    // ------------------------------------------------------------------
    // Account-ordering helpers
    // ------------------------------------------------------------------

    static bool
    holderIsHigh(jtx::Account const& issuer, jtx::Account const& alice)
    {
        return alice.id() > issuer.id();
    }

    // Find an (issuer, alice) name pair such that alice > issuer. The
    // AccountID order is a stable hash of the seed, so we can't request
    // it directly — search a small suffix space. Deterministic for a
    // given @p tag.
    static std::pair<jtx::Account, jtx::Account>
    pickHolderHigh(std::string const& tag)
    {
        using namespace jtx;
        for (int i = 0; i < 200; ++i)
        {
            Account const issuer{"iss_" + tag + "_" + std::to_string(i)};
            Account const alice{"alc_" + tag + "_" + std::to_string(i)};
            if (holderIsHigh(issuer, alice))
                return {issuer, alice};
        }
        Throw<std::runtime_error>("pickHolderHigh: no suffix produced alice > issuer");
        return {Account{"unreachable"}, Account{"unreachable"}};  // LCOV_EXCL_LINE
    }

    // ------------------------------------------------------------------
    // Context bundles + factories
    // ------------------------------------------------------------------

    // Handles shared by every "issuer + one holder" test.
    struct HolderCtx
    {
        jtx::Account issuer;
        jtx::Account alice;
        jtx::PrettyAsset asset;
        Issue iss;
    };

    // Three-account arrangement: alice → carol via issuer.
    struct TransitCtx
    {
        jtx::Account issuer;
        jtx::Account alice;
        jtx::Account carol;
        jtx::PrettyAsset asset;
        Issue iss;
    };

    // Fan-out: alice as sender, bob and carol as receivers.
    struct MultiSendCtx
    {
        jtx::Account issuer;
        jtx::Account alice;
        jtx::Account bob;
        jtx::Account carol;
        jtx::PrettyAsset asset;
        Issue iss;
    };

    static HolderCtx
    makeHolderAccounts(std::string const& tag, bool holderHigh)
    {
        using namespace jtx;
        if (holderHigh)
        {
            auto [issuer, alice] = pickHolderHigh(tag);
            PrettyAsset const asset = issuer["USD"];
            Issue const iss = asset.raw().get<Issue>();
            return HolderCtx{
                .issuer = std::move(issuer), .alice = std::move(alice), .asset = asset, .iss = iss};
        }
        Account issuer{"iss_" + tag};
        Account alice{"alice_" + tag};
        PrettyAsset const asset = issuer["USD"];
        Issue const iss = asset.raw().get<Issue>();
        return HolderCtx{
            .issuer = std::move(issuer), .alice = std::move(alice), .asset = asset, .iss = iss};
    }

    // Fund the accounts, open a trust line, and (optionally) seed an
    // initial balance via a real payment. Common preamble for Groups 1–4,
    // 6, 9, 10.
    static HolderCtx
    setupCtx(
        jtx::Env& env,
        std::string const& tag,
        int initialBal = 100,
        int limit = 10'000,
        bool holderHigh = false)
    {
        auto c = makeHolderAccounts(tag, holderHigh);
        setupHolderLine(env, c.issuer, c.alice, c.asset, c.asset(initialBal), c.asset(limit));
        return c;
    }

    // Set alice's trust line up so that it is eligible for auto-delete
    // when balance zeroes: alice created the line (owns the reserve),
    // limit was zeroed after seeding, no freeze / quality overrides.
    static HolderCtx
    setupAutoDeleteCtx(jtx::Env& env, std::string const& tag, int seedBal = 100)
    {
        using namespace jtx;
        auto c = makeHolderAccounts(tag, /*holderHigh=*/false);
        env.fund(XRP(10'000), c.issuer, c.alice);
        env.close();
        env(trust(c.alice, c.asset(1'000)));
        env.close();
        env(pay(c.issuer, c.alice, c.asset(seedBal)));
        env.close();
        // Zero alice's limit so the auto-delete guard becomes eligible.
        env(trust(c.alice, c.asset(0)));
        env.close();
        return c;
    }

    static TransitCtx
    setupTransit(jtx::Env& env, std::string const& tag, int aliceInitial, int carolInitial)
    {
        using namespace jtx;
        Account issuer{"iss_" + tag};
        Account alice{"alice_" + tag};
        Account carol{"carol_" + tag};
        PrettyAsset const asset = issuer["USD"];
        Issue const iss = asset.raw().get<Issue>();

        env.fund(XRP(10'000), issuer, alice, carol);
        env.close();
        env(trust(alice, asset(1'000'000)));
        env(trust(carol, asset(1'000'000)));
        env.close();
        if (aliceInitial != 0)
            env(pay(issuer, alice, asset(aliceInitial)));
        if (carolInitial != 0)
            env(pay(issuer, carol, asset(carolInitial)));
        env.close();
        return TransitCtx{
            .issuer = std::move(issuer),
            .alice = std::move(alice),
            .carol = std::move(carol),
            .asset = asset,
            .iss = iss};
    }

    static MultiSendCtx
    setupMultiSend(jtx::Env& env, std::string const& tag, int aliceInitial = 1'000)
    {
        using namespace jtx;
        Account issuer{"iss_" + tag};
        Account alice{"alice_" + tag};
        Account bob{"bob_" + tag};
        Account carol{"carol_" + tag};
        PrettyAsset const asset = issuer["USD"];
        Issue const iss = asset.raw().get<Issue>();

        env.fund(XRP(10'000), issuer, alice, bob, carol);
        env.close();
        env(trust(alice, asset(1'000'000)));
        env(trust(bob, asset(1'000'000)));
        env(trust(carol, asset(1'000'000)));
        env.close();
        env(pay(issuer, alice, asset(aliceInitial)));
        env.close();
        return MultiSendCtx{
            .issuer = std::move(issuer),
            .alice = std::move(alice),
            .bob = std::move(bob),
            .carol = std::move(carol),
            .asset = asset,
            .iss = iss};
    }

    // ------------------------------------------------------------------
    // Observation bundle + one-shot runner for direct issuer↔holder sends
    // ------------------------------------------------------------------

    struct LineObservation
    {
        STAmount balAfter = STAmount{};
        Number dustAfter = Number{};
        Number sfDustLedger = Number{};
        bool lineExisted = false;
        TER ter{tesSUCCESS};
    };

    // Configure a single-leg policy (either Override or Drain).
    struct PolicyConfig
    {
        DustSplit::LegPolicy::Mode mode = DustSplit::LegPolicy::Mode::Override;
        int overrideScale = 0;
    };

    // Runs an issuer↔holder direct send with a single-leg policy. The
    // non-issuer party is always `c.alice`, so `dust.receiver` is
    // populated when `holderIsSender=false` and `dust.sender` when true.
    LineObservation
    runDirectSingleLeg(
        jtx::Env& env,
        HolderCtx const& c,
        STAmount const& seedBal,
        Number const& seedDust,
        STAmount const& amount,
        bool holderIsSender,
        PolicyConfig const& cfg,
        DustSplit& outDust)
    {
        LineObservation obs{};
        outDust = DustSplit{};
        auto const legPolicy =
            DustSplit::LegPolicy{.mode = cfg.mode, .overrideScale = cfg.overrideScale};
        if (holderIsSender)
        {
            outDust.sender = legPolicy;
        }
        else
        {
            outDust.receiver = legPolicy;
        }

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            seedLine(sb, c.alice.id(), c.iss, seedBal, seedDust);
            AccountID const& src = holderIsSender ? c.alice.id() : c.issuer.id();
            AccountID const& dst = holderIsSender ? c.issuer.id() : c.alice.id();
            obs.ter = sendVia(sb, j, src, dst, amount, &outDust);
            if (auto const line = sb.peek(keylet::trustLine(c.alice.id(), c.iss)))
            {
                obs.lineExisted = true;
                obs.balAfter = readBalancePartyTerms(*line, c.alice.id(), c.iss.account);
                obs.dustAfter = readDustPartyTerms(*line, c.alice.id(), c.iss.account);
                obs.sfDustLedger = Number{line->at(sfDust)};
            }
        });
        return obs;
    }

    // Convenience: pick the populated leg's policy for out-field
    // inspection. Every test in this suite populates exactly one leg.
    static DustSplit::LegPolicy const*
    activeLeg(DustSplit const& d)
    {
        if (d.sender.has_value())
            return &*d.sender;
        if (d.receiver.has_value())
            return &*d.receiver;
        return nullptr;
    }

    // ==================================================================
    // Group 1: Override mode, receiver leg (direct issuer → holder credit)
    // ==================================================================

    void
    testG1CreditExactAtScale()
    {
        testcase("G1.1 receiver-Override: credit exactly at scale leaves no dust");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g1_1");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            /*seedBal=*/c.asset(100),
            /*seedDust=*/Number{0},
            /*amount=*/STAmount{c.iss, 1, -1},  // 0.1
            /*holderIsSender=*/false,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == STAmount{c.iss, 1001, -1}));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        BEAST_EXPECT(obs.sfDustLedger == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{1, -1}));
            BEAST_EXPECT(leg->dustDelta == beast::kZero);
        }
    }

    void
    testG1CreditSubQuantumOnly()
    {
        testcase(
            "G1.2 receiver-Override: sub-quantum credit lands entirely in "
            "dust");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g1_2");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            /*amount=*/STAmount{c.iss, 3, -9},  // 3e-9
            /*holderIsSender=*/false,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == c.asset(100).value()));
        BEAST_EXPECT((obs.dustAfter == Number{3, -9}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT(leg->balanceDelta == beast::kZero);
            BEAST_EXPECT((leg->dustDelta == Number{3, -9}));
        }
    }

    void
    testG1CreditPromotesDust()
    {
        testcase(
            "G1.3 receiver-Override: credit crossing quantum promotes dust "
            "into balance");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g1_3");

        // Pre-seed dust = 9e-7 in alice's terms; credit 2e-7 at scale -6.
        // extendedAfter = 100 + 9e-7 + 2e-7 = 100.0000011 → truncate toward
        // zero at scale -6: newBalance = 100.000001, newDust = 1e-7.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{9, -7},
            STAmount{c.iss, 2, -7},
            /*holderIsSender=*/false,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == STAmount{c.iss, 100'000'001, -6}));
        BEAST_EXPECT((obs.dustAfter == Number{1, -7}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{1, -6}));
            BEAST_EXPECT((leg->dustDelta == Number{-8, -7}));
        }
    }

    void
    testG1CreditRepeatedPromotion()
    {
        testcase(
            "G1.4 receiver-Override: repeated sub-quantum credits sum "
            "consistently");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g1_4", /*initialBal=*/0);

        // 20 credits of 1e-7 each at scale -6. After k iterations the
        // extended balance is exactly k * 1e-7. Balance jumps by one
        // quantum every 10th iteration.
        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            for (int k = 1; k <= 20; ++k)
            {
                DustSplit d;
                d.receiver = overrideLeg(-6);
                auto const ter =
                    sendVia(sb, j, c.issuer.id(), c.alice.id(), STAmount{c.iss, 1, -7}, &d);
                BEAST_EXPECT(isTesSuccess(ter));

                auto const line = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
                if (!BEAST_EXPECT(line))
                    return;
                Number const bal =
                    Number{readBalancePartyTerms(*line, c.alice.id(), c.iss.account)};
                Number const dust = readDustPartyTerms(*line, c.alice.id(), c.iss.account);
                BEAST_EXPECT(((bal + dust) == Number{k, -7}));
                BEAST_EXPECT((abs(dust) < Number{1, -6}));
                // Every 10th iteration, dust must be exactly zero.
                if (k == 10 || k == 20)
                    BEAST_EXPECT(dust == beast::kZero);
            }
        });
    }

    void
    testG1CreditNoDustAtCoarseScale()
    {
        testcase("G1.5 receiver-Override: at a coarse scale, no dust remainder");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g1_5");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            /*amount=*/STAmount{c.iss, 123, -2},  // 1.23
            /*holderIsSender=*/false,
            {.overrideScale = -2},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == STAmount{c.iss, 10123, -2}));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{123, -2}));
            BEAST_EXPECT(leg->dustDelta == beast::kZero);
        }
    }

    // ==================================================================
    // Group 2: Override mode, sender leg (direct holder → issuer debit)
    // ==================================================================

    void
    testG2DebitConsumesExactly()
    {
        testcase("G2.1 sender-Override: debit consumes balance and dust exactly");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g2_1");

        // Seeded: bal=100, dust=5e-7 (alice terms).
        // Debit: 100.0000005 (STAmount canonical).
        // extendedAfter = 100.0000005 - 100.0000005 = 0.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{5, -7},
            /*amount=*/STAmount{c.iss, 1'000'000'005, -7},
            /*holderIsSender=*/true,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT(Number{obs.balAfter} == beast::kZero);
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{-100}));
            BEAST_EXPECT((leg->dustDelta == Number{-5, -7}));
        }
    }

    void
    testG2DebitDrivesExtendedNegative()
    {
        testcase("G2.2 sender-Override: debit drives extended balance below zero");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g2_2");

        // Seeded: bal=100, dust=0.
        // Debit 100.0000015 at scale -6. extendedAfter = -1.5e-6.
        // Truncated toward zero at -6: newBalance = -1e-6 (magnitude
        // decreases), newDust = -5e-7 in alice's terms.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            /*amount=*/STAmount{c.iss, 1'000'000'015, -7},
            /*holderIsSender=*/true,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((Number{obs.balAfter} == Number{-1, -6}));
        BEAST_EXPECT((obs.dustAfter == Number{-5, -7}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            // Bal went 100 → -1e-6, delta = -100.000001.
            BEAST_EXPECT((leg->balanceDelta == Number{-100'000'001, -6}));
            BEAST_EXPECT((leg->dustDelta == Number{-5, -7}));
        }
    }

    void
    testG2DebitLeavesPositiveDust()
    {
        testcase(
            "G2.3 sender-Override: debit smaller than a quantum leaves "
            "sub-quantum credit dust");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g2_3");

        // Debit 7e-7 at scale -6. extendedBefore=100. extendedAfter=99.9999993.
        // Truncated toward zero: newBalance = 99.999999, newDust = 3e-7.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            /*amount=*/STAmount{c.iss, 7, -7},
            /*holderIsSender=*/true,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == STAmount{c.iss, 99'999'999, -6}));
        BEAST_EXPECT((obs.dustAfter == Number{3, -7}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{-1, -6}));
            BEAST_EXPECT((leg->dustDelta == Number{3, -7}));
        }
    }

    void
    testG2DebitClearsPositiveDust()
    {
        testcase(
            "G2.4 sender-Override: debit exactly equal to seeded dust zeroes "
            "dust, no balance change");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g2_4");

        // Seeded dust=4e-7. Debit=4e-7 at scale -6. extendedBefore=100.0000004,
        // extendedAfter=100. newBalance=100, newDust=0.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{4, -7},
            /*amount=*/STAmount{c.iss, 4, -7},
            /*holderIsSender=*/true,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == c.asset(100).value()));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT(leg->balanceDelta == beast::kZero);
            BEAST_EXPECT((leg->dustDelta == Number{-4, -7}));
        }
    }

    // ==================================================================
    // Group 3: Sign convention — holder is HIGH account (bSenderHigh)
    // ==================================================================

    void
    testG3ReceiverCreditHolderHigh()
    {
        testcase(
            "G3.1 receiver-Override: holder is HIGH account; ledger sfDust "
            "sign is flipped");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g3_1", 100, 10'000, /*holderHigh=*/true);
        BEAST_EXPECT(holderIsHigh(c.issuer, c.alice));

        // Credit 3e-9 at scale -6. Expect: alice's balance 100, dust +3e-9
        // in her terms. On ledger, sfDust is sign-flipped (holder is high),
        // so raw sfDust = -3e-9.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            STAmount{c.iss, 3, -9},
            /*holderIsSender=*/false,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == c.asset(100).value()));
        BEAST_EXPECT((obs.dustAfter == Number{3, -9}));
        // Ledger encoding: sign-flipped.
        BEAST_EXPECT((obs.sfDustLedger == Number{-3, -9}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            // Out-fields are in holder's terms → same as if holder were low.
            BEAST_EXPECT(leg->balanceDelta == beast::kZero);
            BEAST_EXPECT((leg->dustDelta == Number{3, -9}));
        }
    }

    void
    testG3SenderDebitHolderHigh()
    {
        testcase(
            "G3.2 sender-Override: holder is HIGH; out-fields still in holder "
            "terms");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g3_2", 100, 10'000, /*holderHigh=*/true);
        BEAST_EXPECT(holderIsHigh(c.issuer, c.alice));

        // Debit 7e-7 (holder-terms outflow). Same math as G2.3.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            STAmount{c.iss, 7, -7},
            /*holderIsSender=*/true,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == STAmount{c.iss, 99'999'999, -6}));
        BEAST_EXPECT((obs.dustAfter == Number{3, -7}));
        // Ledger raw sfDust is sign-flipped:
        BEAST_EXPECT((obs.sfDustLedger == Number{-3, -7}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{-1, -6}));
            BEAST_EXPECT((leg->dustDelta == Number{3, -7}));
        }
    }

    void
    testG3PromotionFromPositiveDustHolderHigh()
    {
        testcase(
            "G3.3 receiver-Override: holder is HIGH; promotion crosses "
            "quantum boundary");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g3_3", 100, 10'000, /*holderHigh=*/true);
        BEAST_EXPECT(holderIsHigh(c.issuer, c.alice));

        // Same math as G1.3, mirrored across the sign convention.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{9, -7},
            STAmount{c.iss, 2, -7},
            /*holderIsSender=*/false,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == STAmount{c.iss, 100'000'001, -6}));
        BEAST_EXPECT((obs.dustAfter == Number{1, -7}));
        BEAST_EXPECT((obs.sfDustLedger == Number{-1, -7}));
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{1, -6}));
            BEAST_EXPECT((leg->dustDelta == Number{-8, -7}));
        }
    }

    // ==================================================================
    // Group 4: Drain mode (sender-leg only)
    // ==================================================================

    void
    testG4DrainWithNonZeroDust()
    {
        testcase(
            "G4.1 sender-Drain: with non-zero seeded dust, dust folds into "
            "transfer");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g4_1");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{4, -7},
            /*amount=*/c.asset(10).value(),
            /*holderIsSender=*/true,
            {.mode = DustSplit::LegPolicy::Mode::Drain},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        // Alice's line: sfBalance = 100 - 10 = 90; sfDust = 0.
        BEAST_EXPECT((obs.balAfter == c.asset(90).value()));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{-10}));
            BEAST_EXPECT((leg->dustDelta == Number{-4, -7}));
        }
    }

    void
    testG4DrainWithZeroDust()
    {
        testcase(
            "G4.2 sender-Drain: with zero seeded dust, behaves like plain "
            "send");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g4_2");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            c.asset(10).value(),
            /*holderIsSender=*/true,
            {.mode = DustSplit::LegPolicy::Mode::Drain},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == c.asset(90).value()));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{-10}));
            BEAST_EXPECT(leg->dustDelta == beast::kZero);
        }
    }

    void
    testG4DrainZeroAmount()
    {
        testcase(
            "G4.3 sender-Drain: zero-amount call still drains dust into "
            "transfer");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g4_3");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{7, -9},
            /*amount=*/STAmount{c.iss},
            /*holderIsSender=*/true,
            {.mode = DustSplit::LegPolicy::Mode::Drain},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == c.asset(100).value()));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT(leg->balanceDelta == beast::kZero);
            BEAST_EXPECT((leg->dustDelta == Number{-7, -9}));
        }
    }

    void
    testG4DrainHolderHigh()
    {
        testcase("G4.4 sender-Drain: holder is HIGH; sign flow correct end-to-end");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g4_4", 100, 10'000, /*holderHigh=*/true);
        BEAST_EXPECT(holderIsHigh(c.issuer, c.alice));

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{4, -7},
            c.asset(10).value(),
            /*holderIsSender=*/true,
            {.mode = DustSplit::LegPolicy::Mode::Drain},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT((obs.balAfter == c.asset(90).value()));
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
        BEAST_EXPECT(obs.sfDustLedger == beast::kZero);
        if (auto const* leg = activeLeg(d); BEAST_EXPECT(leg))
        {
            BEAST_EXPECT((leg->balanceDelta == Number{-10}));
            BEAST_EXPECT((leg->dustDelta == Number{-4, -7}));
        }
    }

    // ==================================================================
    // Group 5: Transit path — non-issuer sender AND non-issuer receiver
    // ==================================================================

    void
    testG5TransitReceiverOverride()
    {
        testcase("G5.1 transit: receiver-only Override; sender line runs classic");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupTransit(env, "g5_1", 500, 500);

        DustSplit d;
        d.receiver = overrideLeg(-6);

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            // Send 3e-9 alice → carol; the receiver leg (issuer → carol)
            // sees Override at scale -6, so extra dust lands on carol's
            // line, not alice's.
            auto const ter = sendVia(sb, j, c.alice.id(), c.carol.id(), STAmount{c.iss, 3, -9}, &d);
            BEAST_EXPECT(isTesSuccess(ter));

            auto const aliceLine = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            auto const carolLine = sb.peek(keylet::trustLine(c.carol.id(), c.iss));
            if (!BEAST_EXPECT(aliceLine) || !BEAST_EXPECT(carolLine))
                return;

            // Alice's line: no dust touched (nullptr on sender leg here).
            BEAST_EXPECT(Number{aliceLine->at(sfDust)} == beast::kZero);
            // Carol's line: dust grew by exactly 3e-9 in her terms.
            Number const carolDust = readDustPartyTerms(*carolLine, c.carol.id(), c.iss.account);
            BEAST_EXPECT((carolDust == Number{3, -9}));
            if (BEAST_EXPECT(d.receiver.has_value()))
                BEAST_EXPECT((d.receiver->dustDelta == Number{3, -9}));
        });
    }

    void
    testG5TransitSenderOverride()
    {
        testcase("G5.2 transit: sender-only Override; receiver line runs classic");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupTransit(env, "g5_2", 500, 500);

        DustSplit d;
        d.sender = overrideLeg(-6);

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            auto const ter = sendVia(sb, j, c.alice.id(), c.carol.id(), STAmount{c.iss, 3, -9}, &d);
            BEAST_EXPECT(isTesSuccess(ter));

            auto const aliceLine = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            auto const carolLine = sb.peek(keylet::trustLine(c.carol.id(), c.iss));
            if (!BEAST_EXPECT(aliceLine) || !BEAST_EXPECT(carolLine))
                return;

            // Alice starts at bal=500 (exact at scale -6). Debit 3e-9
            // brings extended balance to 499.999999997; truncated toward
            // zero at scale -6 gives newBalance = 499.999999, newDust =
            // +9.97e-7 (in alice's terms). Alice's line lost one whole
            // quantum (1e-6) of balance, and gained 997e-9 of dust — net
            // change = -3e-9.
            BEAST_EXPECT(
                (readBalancePartyTerms(*aliceLine, c.alice.id(), c.iss.account) ==
                 STAmount{c.iss, 499'999'999, -6}));
            Number const aliceDust = readDustPartyTerms(*aliceLine, c.alice.id(), c.iss.account);
            BEAST_EXPECT((aliceDust == Number{997, -9}));
            // Carol's line: no dust touched.
            BEAST_EXPECT(Number{carolLine->at(sfDust)} == beast::kZero);
            if (BEAST_EXPECT(d.sender.has_value()))
            {
                BEAST_EXPECT((d.sender->balanceDelta == Number{-1, -6}));
                BEAST_EXPECT((d.sender->dustDelta == Number{997, -9}));
            }
        });
    }

    void
    testG5TransitBothLegs()
    {
        testcase(
            "G5.3 transit: both legs Override; independent bookkeeping on "
            "each line");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupTransit(env, "g5_3", 500, 500);

        DustSplit d;
        d.sender = overrideLeg(-6);
        d.receiver = overrideLeg(-6);

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            auto const ter = sendVia(sb, j, c.alice.id(), c.carol.id(), STAmount{c.iss, 3, -9}, &d);
            BEAST_EXPECT(isTesSuccess(ter));

            // Sender line: same math as G5.2 (bal=500, debit 3e-9 at scale
            // -6): balance drops by 1e-6, dust becomes +997e-9.
            if (BEAST_EXPECT(d.sender.has_value()))
                BEAST_EXPECT((d.sender->dustDelta == Number{997, -9}));
            // Receiver line: bal=500, credit 3e-9 at scale -6: balance
            // stays, dust becomes +3e-9 (sub-quantum credit, no wrap).
            if (BEAST_EXPECT(d.receiver.has_value()))
                BEAST_EXPECT((d.receiver->dustDelta == Number{3, -9}));
        });
    }

    void
    testG5TransitDrainSenderInflates()
    {
        testcase(
            "G5.4 transit: sender-Drain inflates the amount seen by "
            "receiver-Override");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupTransit(env, "g5_4", 500, 500);

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            // Seed alice's line with 4e-7 of dust; a Drain will fold that
            // into the transfer.
            seedLine(sb, c.alice.id(), c.iss, c.asset(500).value(), Number{4, -7});
            DustSplit d;
            d.sender = drainLeg();
            d.receiver = overrideLeg(-6);

            auto const ter = sendVia(
                sb,
                j,
                c.alice.id(),
                c.carol.id(),
                STAmount{c.iss, 10, 0},  // 10 USD
                &d);
            BEAST_EXPECT(isTesSuccess(ter));

            auto const aliceLine = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            auto const carolLine = sb.peek(keylet::trustLine(c.carol.id(), c.iss));
            if (!BEAST_EXPECT(aliceLine) || !BEAST_EXPECT(carolLine))
                return;

            // Alice's line: balance = 500 - 10 = 490, dust = 0.
            BEAST_EXPECT(
                (readBalancePartyTerms(*aliceLine, c.alice.id(), c.iss.account) ==
                 c.asset(490).value()));
            BEAST_EXPECT(
                readDustPartyTerms(*aliceLine, c.alice.id(), c.iss.account) == beast::kZero);
            // Carol received 10 + 4e-7. Balance grew by 10, dust grew by 4e-7.
            BEAST_EXPECT(
                (readBalancePartyTerms(*carolLine, c.carol.id(), c.iss.account) ==
                 c.asset(510).value()));
            BEAST_EXPECT(
                (readDustPartyTerms(*carolLine, c.carol.id(), c.iss.account) == Number{4, -7}));
        });
    }

    // ==================================================================
    // Group 6: creditBalanceExact
    // ==================================================================

    void
    testG6NoLineReturnsZero()
    {
        testcase("G6.1 creditBalanceExact: absent trust line returns zero");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = makeHolderAccounts("g6_1", /*holderHigh=*/false);
        env.fund(XRP(10'000), c.issuer, c.alice);
        env.close();

        BEAST_EXPECT(creditBalanceExact(*env.current(), c.alice.id(), c.iss) == beast::kZero);
    }

    void
    testG6ZeroDustReturnsBalance()
    {
        testcase(
            "G6.2 creditBalanceExact: dust==0 returns sfBalance in party "
            "terms");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g6_2");

        BEAST_EXPECT((creditBalanceExact(*env.current(), c.alice.id(), c.iss) == Number{100}));
    }

    void
    testG6SumsDust()
    {
        testcase(
            "G6.3 creditBalanceExact: returns sfBalance + sfDust in party "
            "terms");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g6_3");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            seedLine(sb, c.alice.id(), c.iss, c.asset(100).value(), Number{3, -9});
            BEAST_EXPECT(
                (creditBalanceExact(sb, c.alice.id(), c.iss) == (Number{100} + Number{3, -9})));
        });
    }

    void
    testG6SignAliceHigh()
    {
        testcase(
            "G6.4 creditBalanceExact: holder-high sign flip correctly "
            "applied");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g6_4", 100, 10'000, /*holderHigh=*/true);
        BEAST_EXPECT(holderIsHigh(c.issuer, c.alice));

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            seedLine(sb, c.alice.id(), c.iss, c.asset(100).value(), Number{5, -9});
            // Alice is HIGH, so the ledger stores sfBalance = -100 and
            // sfDust = -5e-9. creditBalanceExact must flip both signs to
            // return the value in alice's own terms: +100 + 5e-9.
            BEAST_EXPECT(
                (creditBalanceExact(sb, c.alice.id(), c.iss) == (Number{100} + Number{5, -9})));
            // Sanity: raw ledger encoding is sign-flipped.
            auto const line = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            if (BEAST_EXPECT(line))
                BEAST_EXPECT((Number{line->at(sfDust)} == Number{-5, -9}));
        });
    }

    void
    testG6AmendmentGateOff()
    {
        testcase("G6.5 creditBalanceExact: amendment OFF ignores sfDust");
        using namespace jtx;
        // Compose an explicit FeatureBitset without featureLendingProtocolV1_1
        // so the arithmetic gate is off but the sfDust field is still writable
        // via a direct Sandbox poke.
        FeatureBitset const noDust = all_ - featureLendingProtocolV1_1;
        Env env{*this, noDust};
        auto c = makeHolderAccounts("g6_5", /*holderHigh=*/false);
        setupHolderLine(env, c.issuer, c.alice, c.asset, c.asset(100), c.asset(10'000));

        // Even if we forcibly write sfDust via Sandbox, creditBalanceExact
        // must ignore it under the amendment-off gate.
        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            auto line = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            if (!BEAST_EXPECT(line))
                return;
            line->at(sfDust) = Number{7, -9};
            sb.update(line);
            BEAST_EXPECT((creditBalanceExact(sb, c.alice.id(), c.iss) == Number{100}));
        });
    }

    // ==================================================================
    // Group 7: Auto-delete guard inside directSendNoFeeIOU
    // ==================================================================
    //
    // Direct-send auto-deletes a trust line when: sender balance was
    // positive, ends at zero, sender-reserve flag set, sender limit is 0,
    // sender quality-in/out both zero, sender not frozen, receiver
    // reserve clear. This group verifies the new dust-aware guard blocks
    // deletion whenever sfDust is non-zero (regardless of whether the
    // current call is dust-aware).

    void
    testG7AutoDeleteBlockedByFreshDust()
    {
        testcase("G7.1 auto-delete blocked by fresh Override dust");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupAutoDeleteCtx(env, "g7_1");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            DustSplit d;
            d.sender = overrideLeg(-6);
            // Alice sends 100.0000007 out. Balance drives to -1e-6 (or
            // similar); but here we choose an amount that leaves balance=0
            // and dust=-7e-7 (i.e., sub-quantum "owed" balance).
            // extendedBefore = 100. extendedAfter = -7e-7. newBalance at
            // scale -6 TowardsZero = 0. newDust = -7e-7.
            auto const ter =
                sendVia(sb, j, c.alice.id(), c.issuer.id(), STAmount{c.iss, 1'000'000'007, -7}, &d);
            BEAST_EXPECT(isTesSuccess(ter));
            // Line still exists — the dust guard blocked deletion.
            auto const line = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            BEAST_EXPECT(line != nullptr);
            if (line)
            {
                BEAST_EXPECT(
                    (readBalancePartyTerms(*line, c.alice.id(), c.iss.account) == beast::kZero));
                BEAST_EXPECT(
                    readDustPartyTerms(*line, c.alice.id(), c.iss.account) != beast::kZero);
            }
        });
    }

    void
    testG7AutoDeleteBlockedByPreexistingDust()
    {
        testcase("G7.2 auto-delete blocked by pre-existing dust (nullptr policy)");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupAutoDeleteCtx(env, "g7_2");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            // Seed dust on the line, THEN issue a nullptr-policy
            // accountSend that drains the balance. Under the amendment
            // gate, the guard must still refuse to delete the line.
            auto line = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            if (!BEAST_EXPECT(line))
                return;
            line->at(sfDust) = toLedgerTerms(Number{5, -9}, c.alice.id(), c.iss.account);
            sb.update(line);

            auto const ter =
                sendVia(sb, j, c.alice.id(), c.issuer.id(), c.asset(100).value(), /*d=*/nullptr);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(sb.peek(keylet::trustLine(c.alice.id(), c.iss)) != nullptr);
        });
    }

    void
    testG7AutoDeleteProceedsWhenDustZero()
    {
        testcase("G7.3 auto-delete proceeds when dust is zero");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupAutoDeleteCtx(env, "g7_3");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            // No dust anywhere. Plain send draining alice's balance to zero
            // triggers auto-delete.
            auto const ter =
                sendVia(sb, j, c.alice.id(), c.issuer.id(), c.asset(100).value(), /*d=*/nullptr);
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(sb.peek(keylet::trustLine(c.alice.id(), c.iss)) == nullptr);
        });
    }

    // ==================================================================
    // Group 8: Multi-send
    // ==================================================================

    void
    testG8MultiSendSenderOverrideOnce()
    {
        testcase(
            "G8.1 accountSendMulti: sender-Override applies exactly once to "
            "bulk debit");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupMultiSend(env, "g8_1");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            DustSplit d;
            d.sender = overrideLeg(-6);

            MultiplePaymentDestinations const receivers{
                {c.bob.id(), Number{3, -9}}, {c.carol.id(), Number{2, -9}}};
            auto const ter =
                accountSendMulti(sb, c.alice.id(), c.iss, receivers, j, WaiveTransferFee::Yes, &d);
            BEAST_EXPECT(isTesSuccess(ter));

            // Alice starts at exact-quantum 1000. Bulk debit 5e-9 at scale
            // -6 gives extended = 999.999999995. Truncated toward zero:
            // newBalance = 999.999999, newDust = +995e-9.
            auto const aliceLine = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            if (BEAST_EXPECT(aliceLine))
            {
                BEAST_EXPECT(
                    (readBalancePartyTerms(*aliceLine, c.alice.id(), c.iss.account) ==
                     STAmount{c.iss, 999'999'999, -6}));
                BEAST_EXPECT(
                    (readDustPartyTerms(*aliceLine, c.alice.id(), c.iss.account) ==
                     Number{995, -9}));
            }
            if (BEAST_EXPECT(d.sender.has_value()))
                BEAST_EXPECT((d.sender->dustDelta == Number{995, -9}));
        });
    }

    void
    testG8MultiSendNoDustOnPerRecipientLines()
    {
        testcase("G8.2 accountSendMulti: no sfDust written to receiver lines");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupMultiSend(env, "g8_2");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            DustSplit d;
            d.sender = overrideLeg(-6);

            MultiplePaymentDestinations const receivers{
                {c.bob.id(), Number{10}}, {c.carol.id(), Number{20}}};
            auto const ter =
                accountSendMulti(sb, c.alice.id(), c.iss, receivers, j, WaiveTransferFee::Yes, &d);
            BEAST_EXPECT(isTesSuccess(ter));

            auto const bobLine = sb.peek(keylet::trustLine(c.bob.id(), c.iss));
            auto const carolLine = sb.peek(keylet::trustLine(c.carol.id(), c.iss));
            if (BEAST_EXPECT(bobLine))
                BEAST_EXPECT(Number{bobLine->at(sfDust)} == beast::kZero);
            if (BEAST_EXPECT(carolLine))
                BEAST_EXPECT(Number{carolLine->at(sfDust)} == beast::kZero);
        });
    }

    // ==================================================================
    // Group 9: Round-trip / conservation
    // ==================================================================

    void
    testG9OverrideRoundTrip()
    {
        testcase(
            "G9.1 Override: credit followed by equal debit returns line to "
            "initial state");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g9_1");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            auto const line0 = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            if (!BEAST_EXPECT(line0))
                return;
            STAmount const bal0 = line0->getFieldAmount(sfBalance);
            Number const dust0 = line0->at(sfDust);

            DustSplit d1;
            d1.receiver = overrideLeg(-6);
            BEAST_EXPECT(isTesSuccess(
                sendVia(sb, j, c.issuer.id(), c.alice.id(), STAmount{c.iss, 12'345'678, -7}, &d1)));

            DustSplit d2;
            d2.sender = overrideLeg(-6);
            BEAST_EXPECT(isTesSuccess(
                sendVia(sb, j, c.alice.id(), c.issuer.id(), STAmount{c.iss, 12'345'678, -7}, &d2)));

            auto const lineAfter = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
            if (!BEAST_EXPECT(lineAfter))
                return;
            BEAST_EXPECT(lineAfter->getFieldAmount(sfBalance) == bal0);
            BEAST_EXPECT(Number{lineAfter->at(sfDust)} == dust0);
        });
    }

    void
    testG9ConservationOfExtendedBalance()
    {
        testcase(
            "G9.2 Override: for any send, extended balance changes by "
            "exactly amount");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g9_2");

        withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
            Number const before = creditBalanceExact(sb, c.alice.id(), c.iss);
            DustSplit d;
            d.receiver = overrideLeg(-6);
            BEAST_EXPECT(isTesSuccess(
                sendVia(sb, j, c.issuer.id(), c.alice.id(), STAmount{c.iss, 7, -9}, &d)));
            Number const after = creditBalanceExact(sb, c.alice.id(), c.iss);
            BEAST_EXPECT((after - before == Number{7, -9}));
        });
    }

    void
    testG9Associativity()
    {
        testcase(
            "G9.3 Override: two smaller sends equal one larger send (same "
            "end state)");
        using namespace jtx;

        auto runOne = [&](STAmount const& a, STAmount const& b) {
            Env env{*this, withDust_};
            auto c = setupCtx(env, "g9_3");

            std::pair<STAmount, Number> result;
            withSandbox(env, [&](Sandbox& sb, beast::Journal j) {
                for (auto const& amt : {a, b})
                {
                    if (amt == beast::kZero)
                        continue;
                    DustSplit d;
                    d.receiver = overrideLeg(-6);
                    BEAST_EXPECT(
                        isTesSuccess(sendVia(sb, j, c.issuer.id(), c.alice.id(), amt, &d)));
                }
                auto const line = sb.peek(keylet::trustLine(c.alice.id(), c.iss));
                if (line)
                {
                    result.first = readBalancePartyTerms(*line, c.alice.id(), c.iss.account);
                    result.second = readDustPartyTerms(*line, c.alice.id(), c.iss.account);
                }
            });
            return result;
        };

        // Shared issue for building STAmount inputs. The value itself is
        // scoped to each runOne call, but STAmount construction requires
        // a currency/issuer pair we know at this scope.
        Account const issuer{"iss_g9_3"};
        PrettyAsset const asset = issuer["USD"];
        Issue const iss = asset.raw().get<Issue>();

        auto const single = runOne(STAmount{iss, 3, -7}, STAmount{iss});
        auto const split = runOne(STAmount{iss, 1, -7}, STAmount{iss, 2, -7});
        BEAST_EXPECT(single.first == split.first);
        BEAST_EXPECT(single.second == split.second);
    }

    // ==================================================================
    // Group 10: Boundary / edge conditions
    // ==================================================================

    void
    testG10OverrideScaleIntegral()
    {
        testcase(
            "G10.1 Override scale=0 (integer): fractional inputs land "
            "entirely in dust");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g10_1");

        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{0},
            /*amount=*/STAmount{c.iss, 25, -2},  // 0.25
            /*holderIsSender=*/false,
            {.overrideScale = 0},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        // At scale=0, only whole units land in balance. Balance stays at
        // 100; the 0.25 lands in dust.
        BEAST_EXPECT((obs.balAfter == c.asset(100).value()));
        BEAST_EXPECT((obs.dustAfter == Number{25, -2}));
    }

    void
    testG10ZeroExtendedBalance()
    {
        testcase(
            "G10.2 Override: credit exactly negating extended balance zeroes "
            "both fields");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g10_2");

        // Seed dust=5e-7, then debit exactly 100.0000005. extendedAfter=0.
        DustSplit d;
        auto obs = runDirectSingleLeg(
            env,
            c,
            c.asset(100),
            Number{5, -7},
            STAmount{c.iss, 1'000'000'005, -7},
            /*holderIsSender=*/true,
            {.overrideScale = -6},
            d);

        BEAST_EXPECT(isTesSuccess(obs.ter));
        BEAST_EXPECT(Number{obs.balAfter} == beast::kZero);
        BEAST_EXPECT(obs.dustAfter == beast::kZero);
    }

    void
    testG10DustBoundedByOneQuantum()
    {
        testcase(
            "G10.3 Override: for random-ish sub-quantum inputs, |dust| < one "
            "quantum");
        using namespace jtx;
        Env env{*this, withDust_};
        auto c = setupCtx(env, "g10_3", /*initialBal=*/1'000, /*limit=*/1'000'000);

        int const scale = -6;
        for (int m = 1; m <= 999; m += 37)
        {
            DustSplit d;
            auto obs = runDirectSingleLeg(
                env,
                c,
                c.asset(1'000),
                Number{0},
                STAmount{c.iss, m, -9},
                /*holderIsSender=*/false,
                {.overrideScale = scale},
                d);
            BEAST_EXPECT(isTesSuccess(obs.ter));
            BEAST_EXPECT((abs(obs.dustAfter) < Number{1, scale}));
        }
    }

public:
    void
    run() override
    {
        // Group 1
        testG1CreditExactAtScale();
        testG1CreditSubQuantumOnly();
        testG1CreditPromotesDust();
        testG1CreditRepeatedPromotion();
        testG1CreditNoDustAtCoarseScale();

        // Group 2
        testG2DebitConsumesExactly();
        testG2DebitDrivesExtendedNegative();
        testG2DebitLeavesPositiveDust();
        testG2DebitClearsPositiveDust();

        // Group 3
        testG3ReceiverCreditHolderHigh();
        testG3SenderDebitHolderHigh();
        testG3PromotionFromPositiveDustHolderHigh();

        // Group 4
        testG4DrainWithNonZeroDust();
        testG4DrainWithZeroDust();
        testG4DrainZeroAmount();
        testG4DrainHolderHigh();

        // Group 5
        testG5TransitReceiverOverride();
        testG5TransitSenderOverride();
        testG5TransitBothLegs();
        testG5TransitDrainSenderInflates();

        // Group 6
        testG6NoLineReturnsZero();
        testG6ZeroDustReturnsBalance();
        testG6SumsDust();
        testG6SignAliceHigh();
        testG6AmendmentGateOff();

        // Group 7
        testG7AutoDeleteBlockedByFreshDust();
        testG7AutoDeleteBlockedByPreexistingDust();
        testG7AutoDeleteProceedsWhenDustZero();

        // Group 8
        testG8MultiSendSenderOverrideOnce();
        testG8MultiSendNoDustOnPerRecipientLines();

        // Group 9
        testG9OverrideRoundTrip();
        testG9ConservationOfExtendedBalance();
        testG9Associativity();

        // Group 10
        testG10OverrideScaleIntegral();
        testG10ZeroExtendedBalance();
        testG10DustBoundedByOneQuantum();
    }
};

BEAST_DEFINE_TESTSUITE(DustSplitArithmetic, app, xrpl);

}  // namespace xrpl::test
