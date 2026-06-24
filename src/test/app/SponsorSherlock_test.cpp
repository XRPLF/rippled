#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/delegate.h>
#include <test/jtx/did.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

// static STAmount
// accountReserve(jtx::Env& env, std::uint32_t count = 1)
// {
//     return env.current()->fees().reserve * count;
// }

static XRPAmount
reserve(jtx::Env& env, std::uint32_t ownerCount)
{
    return baseAccountReserve(*env.current(), ownerCount);
}

static void
adjustAccountXRPBalance(jtx::Env& env, jtx::Account const& account, STAmount const& balanceTo)
{
    using namespace test::jtx;
    XRPL_ASSERT(isXRP(balanceTo), "adjustAccountXRPBalance: balanceTo must be XRP");
    auto const currentBalance = env.balance(account);
    if (currentBalance == balanceTo)
        return;

    auto const baseFee = env.current()->fees().base;
    if (currentBalance > balanceTo)
    {
        env(pay(account, env.master, currentBalance - (balanceTo)),
            Fee(XRP(1)),
            sponsor::As(env.master, spfSponsorFee),
            Sig(sfSponsorSignature, env.master));
    }
    else
    {
        env(pay(env.master, account, balanceTo - currentBalance), Fee(baseFee));
    }

    env.close();
}

static void
fillQueue(jtx::Env& env, jtx::Account const& account)
{
    using namespace jtx;
    auto metrics = env.app().getTxQ().getMetrics(*env.current());
    for (std::uint32_t i = metrics.txInLedger; i <= metrics.txPerLedger; ++i)
        env(noop(account));
}

class SponsorSherlock_test : public beast::unit_test::Suite, public test::jtx::XChainBridgeObjects
{
protected:
    void
    test168CoSignedBlockedWithFeeOnlySponsorship()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/168
        testcase("Co-signed SponsorshipTransfer blocked with Fee-only Sponsorship");
        using namespace test::jtx;
        Env env(*this);

        auto const sponsor = Account("sponsor");
        auto const sponsee = Account("sponsee");

        env.fund(XRP(10000), sponsor);
        env.fund(XRP(1000), sponsee);
        env.close();

        // Create Fee-only sponsorship (no ReserveCount)
        env(sponsor::set_fee(sponsor, 0, XRP(100)), sponsor::SponseeAcc(sponsee));
        env.close();

        // Sponsee creates a DID
        env(did::setValid(sponsee));
        env.close();

        auto const didKey = keylet::did(sponsee.id());

        // Sponsor tries to co-sign SponsorshipTransfer to sponsor the DID
        // This SHOULD work (sponsor has 10000 XRP) but FAILS with tecINSUFFICIENT_RESERVE

        // TEAM DECISION: considered as legit behavior, no fallbacks, sponsor should look after
        // their Sponsorship objects
        env(sponsor::transfer(sponsee, tfSponsorshipCreate, didKey.key),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tecINSUFFICIENT_RESERVE));
    }

    void
    test251AMMDepositRejectXRPDeposits()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/251
        // AMMDeposit::preclaim misroutes !sle as accountCountDelta and hardcodes ownerCountDelta=1,
        // over-charging reserve check by 1 increment (2 XRP) when LP trustline exists (!sle=0).
        testcase("AMMDeposit rejects XRP deposits with insufficient reserve");
        using namespace test::jtx;

        Env env(*this);

        auto const issuer = Account("issuer");
        auto const alice = Account("alice");

        // Fund accounts with initial amounts
        env.fund(XRP(1000), issuer);
        env.fund(XRP(1000), alice);
        env.close();

        // Step 1: Issuer sets DefaultRipple flag
        env(fset(issuer, asfDefaultRipple));
        env.close();

        // Step 2: Alice creates trustline for TKN
        auto const tkn = issuer["TKN"];
        env(trust(alice, tkn(1'000'000)));
        env.close();

        // Step 3: Issuer sends 100 TKN to Alice
        env(pay(issuer, alice, tkn(100)));
        env.close();

        // Verify Alice has ownerCount=1 (TKN trustline only)
        BEAST_EXPECT(env.ownerCount(alice) == 1);

        // Step 4: Alice creates AMM (100 TKN / 5 XRP)
        AMM amm(env, alice, tkn(100), XRP(5), CreateArg{.tfee = 500});

        // Verify Alice now has ownerCount=2 (TKN trustline + LP trustline)
        BEAST_EXPECT(env.ownerCount(alice) == 2);

        // Step 5: Drain Alice's balance down to approximately 15 XRP total
        // Target: Leave Alice with just above 15 XRP
        // With ownerCount=2, reserve requirement is base(10) + 2*inc(2) = 14 XRP
        // So 15 XRP + fees should be sufficient (15 >= 14)
        auto currentBalance = env.balance(alice);
        auto const targetBalance = drops(15'001'000);  // 15.001 XRP
        auto const baseFee = env.current()->fees().base;
        auto const reserve2 = reserve(env, 2);  // Reserve for ownerCount=2 (should be 14 XRP)

        // Calculate how much to drain
        // We want final balance = targetBalance
        // Payment equation: currentBalance - drainAmount - Fee = targetBalance
        // So: drainAmount = currentBalance - targetBalance - Fee
        if (currentBalance > targetBalance + baseFee)
        {
            auto const drainAmount = currentBalance - targetBalance - baseFee;

            // Sanity check: ensure Alice can actually send this
            // (must keep at least reserve2)
            if (currentBalance - drainAmount - baseFee >= reserve2)
            {
                env(pay(alice, issuer, drainAmount), Fee(baseFee));
                env.close();
            }
        }

        auto const aliceBalance = env.balance(alice);
        BEAST_EXPECT(env.ownerCount(alice) == 2);
        // Verify Alice has a reasonable balance above reserve but not too much
        // Reserve for ownerCount=2 is 14 XRP, so Alice should have >= 14 XRP
        BEAST_EXPECT(aliceBalance >= reserve2);

        // Step 6: Alice attempts AMMDeposit with 1 drop XRP
        // With ownerCount=2 and LP trustline exists (!sle=0 since she has LP trustline):
        //   Correct behavior: reserve_for(N=2, !sle=0) = base + 2*inc = 10 + 2*2 = 14 XRP
        //   Post-deposit balance ~= 15 XRP - 1 drop ≈ 15 XRP. 15 >= 14 -> should PASS.
        //
        // Bug (issue #251): reserve_for(N+1=3, accountCountDelta=0) = base + 3*inc = 10 + 6 = 16
        // XRP.
        //                   15 < 16 -> would fail with tecINSUF_RESERVE_LINE.
        //
        // This test verifies the bug is FIXED - the deposit should succeed.
        amm.deposit(alice, {}, drops(1), {}, {}, tfSingleAsset, {}, {});
    }

    void
    test750SponsorFeeQueueAdmissionBug()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/750

        testcase("SponsorFeeQueueAdmissionBug");
        using namespace jtx;

        auto cfg = makeConfig({{"minimum_txn_in_ledger_standalone", "3"}});
        cfg->fees.referenceFee = 10;
        cfg->fees.accountReserve = 200;
        cfg->fees.ownerReserve = 50;
        Env env{*this, std::move(cfg)};

        Account const sponsorAcc{"sponsor"};
        Account const alice{"alice"};

        env.fund(XRP(10'000), noripple(sponsorAcc));
        env.fund(XRP(1), noripple(alice));
        env.close();

        env(sponsor::set(sponsorAcc, 0, std::nullopt, XRP(1000)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)));
        env.close();

        auto const sponsorshipKey = keylet::sponsorship(sponsorAcc.id(), alice.id());
        auto const sponsorshipSle = env.current()->read(sponsorshipKey);
        if (!BEAST_EXPECT(sponsorshipSle))
            return;
        auto const feeAmountBefore = (*sponsorshipSle)[sfFeeAmount].xrp();
        BEAST_EXPECT(feeAmountBefore == XRP(1000).value().xrp());

        auto const aliceBalBefore = env.balance(alice).value().xrp();

        Account const burner{"burner"};
        env.fund(XRP(1'000'000), burner);
        env.close();
        fillQueue(env, burner);
        fillQueue(env, burner);

        auto const queueFeePerTx = XRPAmount{150};
        auto const aliceSeq = env.seq(alice);

        env(pay(alice, sponsorAcc, drops(1)),
            Fee(queueFeePerTx),
            sponsor::As(sponsorAcc, spfSponsorFee),
            Seq(aliceSeq),
            Ter(terQUEUED));

        env(pay(alice, sponsorAcc, drops(1)),
            Fee(queueFeePerTx),
            sponsor::As(sponsorAcc, spfSponsorFee),
            Seq(aliceSeq + 1),
            Ter(terQUEUED));

        env(pay(alice, sponsorAcc, drops(1)),
            Fee(queueFeePerTx),
            sponsor::As(sponsorAcc, spfSponsorFee),
            Seq(aliceSeq + 2),
            Ter(terQUEUED));  // fixed, originally telCAN_NOT_QUEUE_BALANCE

        auto const aliceBalAfter = env.balance(alice).value().xrp();
        BEAST_EXPECT(aliceBalAfter == aliceBalBefore);

        auto const sponsorshipSleAfter = env.current()->read(sponsorshipKey);
        if (!BEAST_EXPECT(sponsorshipSleAfter))
            return;
        auto const feeAmountAfter = (*sponsorshipSleAfter)[sfFeeAmount].xrp();
        BEAST_EXPECT(feeAmountAfter == feeAmountBefore);
    }

    void
    test750AdversarialSponsorBlocksVictim()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/750

        testcase("AdversarialSponsorBlocksVictim");
        using namespace jtx;

        auto cfg = makeConfig({{"minimum_txn_in_ledger_standalone", "3"}});
        cfg->fees.referenceFee = 10;
        cfg->fees.accountReserve = 200;
        cfg->fees.ownerReserve = 50;
        Env env{*this, std::move(cfg)};

        Account const legitSponsor{"legitSponsor"};
        Account const attacker{"attacker"};
        Account const victim{"victim"};

        env.fund(XRP(10'000), noripple(legitSponsor));
        env.fund(XRP(10'000), noripple(attacker));
        env.fund(XRP(1), noripple(victim));
        env.close();

        // Legitimate sponsor opens a pre-funded sponsorship for victim.
        env(sponsor::set(legitSponsor, 0, std::nullopt, XRP(1000)),
            sponsor::SponseeAcc(victim),
            Fee(XRP(1)));
        env.close();

        // Attacker ALSO opens a pre-funded sponsorship for victim — no
        // consent from victim is required; the ltSPONSORSHIP is keyed
        // by (sponsor, sponsee) pairs.
        env(sponsor::set(attacker, 0, std::nullopt, XRP(1000)),
            sponsor::SponseeAcc(victim),
            Fee(XRP(1)));
        env.close();

        auto const attackerSponsorshipKey = keylet::sponsorship(attacker.id(), victim.id());
        auto const legitSponsorshipKey = keylet::sponsorship(legitSponsor.id(), victim.id());
        BEAST_EXPECT(env.current()->read(attackerSponsorshipKey));
        BEAST_EXPECT(env.current()->read(legitSponsorshipKey));

        auto const victimBalBefore = env.balance(victim).value().xrp();

        Account const burner{"burner"};
        env.fund(XRP(1'000'000), burner);
        env.close();
        fillQueue(env, burner);
        fillQueue(env, burner);

        auto const queueFeePerTx = XRPAmount{150};
        auto const victimSeq = env.seq(victim);

        // Adversarial txs: victim-sender, attacker-sponsor. These fill
        // the queue under victim's account-track.
        env(pay(victim, attacker, drops(1)),
            Fee(queueFeePerTx),
            sponsor::As(attacker, spfSponsorFee),
            Seq(victimSeq),
            Ter(terQUEUED));

        env(pay(victim, attacker, drops(1)),
            Fee(queueFeePerTx),
            sponsor::As(attacker, spfSponsorFee),
            Seq(victimSeq + 1),
            Ter(terQUEUED));

        // Victim's own legitimate sponsored tx is rejected — prior
        // totalFee = 150 + 150 = 300 ≥ reserve 200 — even though
        // legitSponsor has 1000 XRP pre-funded.
        env(pay(victim, legitSponsor, drops(1)),
            Fee(queueFeePerTx),
            sponsor::As(legitSponsor, spfSponsorFee),
            Seq(victimSeq + 2),
            Ter(terQUEUED));  // fixed, originally telCAN_NOT_QUEUE_BALANCE

        auto const victimBalAfter = env.balance(victim).value().xrp();
        BEAST_EXPECT(victimBalAfter == victimBalBefore);
    }

    void
    test1033SponsoredWitnessCanChargeDoorOwnedClaimObjectsToUnrelatedSponsor()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1033

        testcase("xchain account-create attestation redirects witness sponsorship to the door");
        using namespace test::jtx;

        Env mcEnv{*this, envconfig(), features};
        Env scEnv{*this, envconfig(), features};

        Account const sponsor{"sponsor"};
        Account const submitter{"submitter"};
        auto const& witnessSigner = signers[0];

        STAmount const funds{XRP(10000)};
        mcEnv.fund(funds, mcDoor, mcAlice, mcBob, mcCarol, mcGw);
        scEnv.fund(funds, scDoor, scAlice, scBob, scCarol, scGw, sponsor, submitter);
        for (auto const& s : signers)
        {
            mcEnv.fund(funds, s.account);
            scEnv.fund(funds, s.account);
        }
        for (auto const& payee : payees)
            scEnv.fund(funds, payee);
        mcEnv.close();
        scEnv.close();

        auto const& door = scEnv.master;

        mcEnv(jtx::signers(mcDoor, quorum, signers));
        mcEnv(bridgeCreate(mcDoor, jvb, reward, XRP(20)));
        mcEnv.close();

        scEnv(jtx::signers(door, quorum, signers));
        scEnv(bridgeCreate(door, jvb, reward, XRP(20)));
        scEnv.close();

        scEnv(sponsor::set_reserve(sponsor, 0, 1), sponsor::SponseeAcc(submitter));
        scEnv.close();

        std::uint32_t constexpr redirectedClaims = 12;
        auto const baseReserve = reserve(scEnv, 1);
        auto const backedForRedirectedClaims = reserve(scEnv, 1 + redirectedClaims);

        adjustAccountXRPBalance(scEnv, sponsor, drops(backedForRedirectedClaims));

        auto sponsorObj = scEnv.le(keylet::sponsorship(sponsor, submitter));
        BEAST_EXPECT(sponsorObj);
        BEAST_EXPECT(sponsorObj->getFieldU32(sfRemainingOwnerCount) == 1);

        auto const noJournal = beast::Journal{beast::Journal::getNullSink()};
        auto const liquidBefore = xrpLiquid(*scEnv.current(), sponsor.id(), 0, noJournal);
        BEAST_EXPECT(liquidBefore == backedForRedirectedClaims - baseReserve);

        auto const submitterOwnerCountBefore = scEnv.ownerCount(submitter);
        auto const submitterSponsoredOwnerCountBefore = scEnv.sponsoredOwnerCount(submitter);
        auto const doorOwnerCountBefore = scEnv.ownerCount(door);
        auto const doorSponsoredOwnerCountBefore = scEnv.sponsoredOwnerCount(door);
        auto const sponsorSponsoringOwnerCountBefore = scEnv.sponsoringOwnerCount(sponsor);

        scEnv(
            createAccountAttestation(
                submitter,
                jvb,
                mcAlice,
                XRP(20),
                reward,
                payees[0],
                true,
                1,
                scuAlice,
                witnessSigner),
            sponsor::As(sponsor, spfSponsorReserve),
            Ter(tesSUCCESS));
        scEnv.close();

        auto const claim1 = scEnv.le(keylet::xChainCreateAccountClaimID(STXChainBridge(jvb), 1));
        sponsorObj = scEnv.le(keylet::sponsorship(sponsor, submitter));
        BEAST_EXPECT(claim1);
        BEAST_EXPECT(sponsorObj);
        BEAST_EXPECT((*claim1)[sfAccount] == door.id());
        BEAST_EXPECT(!claim1->isFieldPresent(sfSponsor));
        BEAST_EXPECT(sponsorObj->getFieldU32(sfRemainingOwnerCount) == 1);
        BEAST_EXPECT(scEnv.ownerCount(submitter) == submitterOwnerCountBefore);
        BEAST_EXPECT(scEnv.sponsoredOwnerCount(submitter) == submitterSponsoredOwnerCountBefore);
        BEAST_EXPECT(scEnv.ownerCount(door) == doorOwnerCountBefore + 1);
        BEAST_EXPECT(scEnv.sponsoredOwnerCount(door) == doorSponsoredOwnerCountBefore);
        BEAST_EXPECT(scEnv.sponsoringOwnerCount(sponsor) == sponsorSponsoringOwnerCountBefore);

        auto const liquidAfterFirst = xrpLiquid(*scEnv.current(), sponsor.id(), 0, noJournal);
        BEAST_EXPECT(liquidAfterFirst == backedForRedirectedClaims - reserve(scEnv, 1));

        for (std::uint32_t i = 2; i <= redirectedClaims; ++i)
        {
            scEnv(
                createAccountAttestation(
                    submitter,
                    jvb,
                    mcBob,
                    XRP(20),
                    reward,
                    payees[(i - 1) % payees.size()],
                    true,
                    i,
                    scuBob,
                    witnessSigner),
                sponsor::As(sponsor, spfSponsorReserve),
                Ter(tesSUCCESS));
            scEnv.close();

            auto const claim = scEnv.le(keylet::xChainCreateAccountClaimID(STXChainBridge(jvb), i));
            sponsorObj = scEnv.le(keylet::sponsorship(sponsor, submitter));
            BEAST_EXPECT(claim);
            BEAST_EXPECT(sponsorObj);
            BEAST_EXPECT((*claim)[sfAccount] == door.id());
            BEAST_EXPECT(!claim->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sponsorObj->getFieldU32(sfRemainingOwnerCount) == 1);
        }

        BEAST_EXPECT(scEnv.ownerCount(door) == doorOwnerCountBefore + redirectedClaims);
        BEAST_EXPECT(scEnv.sponsoredOwnerCount(door) == doorSponsoredOwnerCountBefore);
        BEAST_EXPECT(scEnv.sponsoringOwnerCount(sponsor) == sponsorSponsoringOwnerCountBefore);

        auto const liquidAfterAll = xrpLiquid(*scEnv.current(), sponsor.id(), 0, noJournal);
        BEAST_EXPECT(liquidAfterAll > reserve(scEnv, 1));

        scEnv(pay(sponsor, scBob, drops(1)), Fee(scEnv.current()->fees().base));
        scEnv.close();
    }

    void
    test1186AMMCreateUsesPreFeeReserveBalance()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1186

        testcase("AMMCreate creates undercollateralized LP-token trustline");

        using namespace jtx;

        auto const run = [&](bool exploitPath) {
            Env env{*this, testableAmendments()};
            auto const fee = env.current()->fees().base;
            auto const ownerIncrement = env.current()->fees().increment;

            Account const gw{"gw"};
            Account const issuer{"issuer"};
            Account const alice{"alice"};
            auto const usd = issuer["USD"];

            env.fund(XRP(100'000), gw, issuer, alice);
            env(fset(issuer, asfDefaultRipple));
            env.close();

            env.trust(usd(50'000), alice);
            env(pay(issuer, alice, usd(30'000)));
            env.close();

            MPTTester const btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = kMptDexFlags});

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(env.le(keylet::line(alice, usd)) != nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(btc.issuanceID(), alice)) != nullptr);

            STAmount const preTrimBalance = env.balance(alice, XRP);
            STAmount const targetBalance =
                exploitPath ? reserve(env, 3) : reserve(env, 3) - drops(1);
            STAmount const trimAmount = preTrimBalance - targetBalance - fee;

            BEAST_EXPECT(trimAmount > XRP(0));
            env(pay(alice, issuer, trimAmount));
            env.close();

            BEAST_EXPECT(env.balance(alice, XRP) == targetBalance);
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            if (!exploitPath)
            {
                AMM const noCreate(
                    env, alice, usd(10'000), btc(10'000), Ter(tecINSUF_RESERVE_LINE));
                BEAST_EXPECT(!noCreate.ammExists());
                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(env.balance(alice, XRP) == targetBalance - ownerIncrement);
                return;
            }

            AMM const amm(env, alice, usd(10'000), btc(10'000));

            auto const lpLine = env.le(keylet::line(alice, amm.lptIssue()));
            // log << "ammcreate_balance=" << env.balance(alice, XRP)
            //     << " owners=" << ownerCount(env, alice) << " lpLineExists=" << (lpLine !=
            //     nullptr)
            //     << std::endl;

            BEAST_EXPECT(amm.ammExists());
            BEAST_EXPECT(lpLine != nullptr);
            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(env.balance(alice, XRP) == targetBalance - ownerIncrement);
            BEAST_EXPECT(env.balance(alice, XRP) < reserve(env, 3));
        };

        run(false);
        run(true);
    }

    void
    test1186AMMDepositUsesPreFeeReserveBalance()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1186
        // TEAM DECISION: considered as not a bug, see TicketCreate::doApply():
        //      "we want to allow dipping into the reserve to pay fees"
        //      As designed

        testcase("AMMDeposit creates undercollateralized LP-token trustline");

        using namespace jtx;

        auto const run = [&](bool exploitPath) {
            Env env{*this, testableAmendments()};
            auto const fee = env.current()->fees().base;

            Account const gw{"gw"};
            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            auto const usd = issuer["USD"];

            env.fund(XRP(100'000), gw, issuer, alice, bob);
            env(fset(issuer, asfDefaultRipple));
            env.close();

            env.trust(usd(50'000), alice);
            env.trust(usd(50'000), bob);
            env(pay(issuer, alice, usd(30'000)));
            env(pay(issuer, bob, usd(30'000)));
            env.close();

            MPTTester const btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 30'000,
                 .flags = kMptDexFlags});

            AMM amm(env, alice, usd(10'000), btc(10'000));
            BEAST_EXPECT(amm.ammExists());

            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(env.le(keylet::line(bob, amm.lptIssue())) == nullptr);

            STAmount const preTrimBalance = env.balance(bob, XRP);
            STAmount const targetBalance =
                exploitPath ? reserve(env, 3) : reserve(env, 3) - drops(1);
            STAmount const trimAmount = preTrimBalance - targetBalance - fee;

            BEAST_EXPECT(trimAmount > XRP(0));
            env(pay(bob, issuer, trimAmount));
            env.close();

            BEAST_EXPECT(env.balance(bob, XRP) == targetBalance);
            BEAST_EXPECT(ownerCount(env, bob) == 2);

            if (!exploitPath)
            {
                amm.deposit(
                    {.account = bob,
                     .asset1In = usd(1'000),
                     .asset2In = btc(1'000),
                     .flags = tfTwoAsset,
                     .err = Ter(tecINSUF_RESERVE_LINE)});

                BEAST_EXPECT(env.le(keylet::line(bob, amm.lptIssue())) == nullptr);
                BEAST_EXPECT(ownerCount(env, bob) == 2);
                BEAST_EXPECT(env.balance(bob, XRP) == targetBalance - fee);
                return;
            }

            amm.deposit(
                {.account = bob,
                 .asset1In = usd(1'000),
                 .asset2In = btc(1'000),
                 .flags = tfTwoAsset});

            auto const lpLine = env.le(keylet::line(bob, amm.lptIssue()));
            // log << "ammdeposit_balance=" << env.balance(bob, XRP)
            //     << " owners=" << ownerCount(env, bob) << " lpLineExists=" << (lpLine != nullptr)
            //     << " lpTokens=" << amm.getLPTokensBalance(bob.id()) << std::endl;

            BEAST_EXPECT(lpLine != nullptr);
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(amm.getLPTokensBalance(bob.id()) > beast::kZero);
            BEAST_EXPECT(env.balance(bob, XRP) == targetBalance - fee);
            BEAST_EXPECT(env.balance(bob, XRP) < reserve(env, 3));
        };

        run(false);
        run(true);
    }

    void
    test1350ReserveCountSilentWrap(FeatureBitset features)
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1350

        testcase(
            "adjustReserveCount uint32 wraparound silently erases pool quota at UINT32_MAX "
            "boundary");

        using namespace test::jtx;

        Env env{*this, features};
        Account const sponsor{"sponsor"};
        Account const sponsee{"sponsee"};
        env.fund(XRP(10000), sponsor, sponsee);
        env.close();

        std::uint32_t const uint32Max = std::numeric_limits<std::uint32_t>::max();

        // STEP 1: Sponsee creates a Check co-signed by sponsor BEFORE any sponsorship
        // exists. adjustOwnerCount in CheckCreate peeks keylet::sponsorship(S, B)
        // — finds nothing — so the pool quota is NOT debited. The resulting
        // ltCHECK carries sfSponsor = sponsor.
        auto const sponseeSeq = env.seq(sponsee);
        uint256 const checkID = keylet::check(sponsee, sponseeSeq).key;
        if (!BEAST_EXPECT(checkID.isNonZero()))
            return;

        env(check::create(sponsee, sponsor, XRP(1)),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();

        // STEP 2: Sponsor (or a SponsorReserve-narrowed delegate) creates the
        // ltSPONSORSHIP pool with sfRemainingOwnerCount at the UINT32_MAX boundary.
        // Because the Check was already created before this pool, no deduction
        // has occurred — the pool starts at exactly UINT32_MAX.
        env(sponsor::set(sponsor, 0, uint32Max), sponsor::SponseeAcc(sponsee));
        env.close();

        // VERIFY 1: pool exists with sfRemainingOwnerCount == UINT32_MAX
        {
            json::Value const poolEntry = sponsor::ledgerEntry(env, sponsor, sponsee);
            auto const& node = poolEntry[jss::result][jss::node];
            BEAST_EXPECT(node.isMember(sfRemainingOwnerCount.jsonName)) &&
                BEAST_EXPECT(node[sfRemainingOwnerCount.jsonName].asUInt() == uint32Max);
        }

        // STEP 3: Sponsee ends the sponsorship on the Check. The End's payback
        // at SponsorshipTransfer.cpp:520-528 calls adjustReserveCount(+1) with
        // sfRemainingOwnerCount = UINT32_MAX. The uint32 addition wraps to 0, int32_t
        // conversion gives 0 (not negative — guard NOT triggered), and the
        // code calls makeFieldAbsent — silently erasing the entire quota.

        // Updated flow: sfRemainingOwnerCount adjustment can only decrease. Freeing object doesn't
        // reset sfRemainingOwnerCount
        env(sponsor::transfer(sponsee, tfSponsorshipEnd, checkID));
        env.close();

        // VERIFY 2: silent wrap — sfRemainingOwnerCount is now ABSENT
        // Present after fix.

        {
            json::Value const poolEntry = sponsor::ledgerEntry(env, sponsor, sponsee);
            auto const& node = poolEntry[jss::result][jss::node];
            BEAST_EXPECT(node.isMember(sfRemainingOwnerCount.jsonName)) &&
                BEAST_EXPECTS(
                    node[sfRemainingOwnerCount.jsonName].asUInt() == uint32Max,
                    std::to_string(node[sfRemainingOwnerCount.jsonName].asUInt()));
        }

        // VERIFY 3: pool is bricked — future drawdowns fail with
        // tecINSUFFICIENT_RESERVE (sfRemainingOwnerCount absent = 0 < ownerCountDelta
        // = 1 at checkInsufficientReserve, preclaim). The Check is now
        // un-sponsored (sfSponsor removed in STEP 3); attempt to re-sponsor it.

        // ALREADY FIXED
        env(sponsor::transfer(sponsee, tfSponsorshipCreate, checkID),
            sponsor::As(sponsor, spfSponsorReserve));
        env.close();
    }

    void
    test1365OracleReserveDecreaseRejection()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1365
        // Oracle reserve decrease (2 -> 1) is incorrectly rejected when spfSponsorReserve is
        // present The signed/unsigned comparison bug in checkInsufficientReserve would cause
        // sponsored Oracle operators to be unable to execute reserve-releasing OracleSet updates
        // FIX: Using std::cmp_less for safe signed/unsigned comparison in checkXrpBalanceGeneral

        testcase("Oracle reserve decrease 2->1 (FIXED)");

        using namespace test::jtx;
        using namespace std::chrono;

        // Test both cosigning and prefunded paths
        for (bool cosigning : {true, false})
        {
            Env env{*this, testableAmendments()};

            Account const sponsor{"sponsor"};
            Account const alice{"alice"};

            env.fund(XRP(10000), sponsor, alice);
            env.close();

            // Helper function to submit sponsored transactions
            auto const submitSponsored = [&](json::Value const& jv, TER expected) {
                if (cosigning)
                {
                    env(jv,
                        sponsor::As(sponsor, spfSponsorReserve),
                        Sig(sfSponsorSignature, sponsor),
                        Ter(expected));
                }
                else
                {
                    env(jv, sponsor::As(sponsor, spfSponsorReserve), Ter(expected));
                }
                env.close();
            };

            // Helper to create OracleSet transaction
            auto const oracleSet = [&](Account const& account, uint8_t dataSeriesSize) {
                auto const now = env.timeKeeper().now();
                env.close(now + oracle::kTestStartTime - kEpochOffset);

                json::Value jv;
                jv[jss::TransactionType] = jss::OracleSet;
                jv[jss::Account] = to_string(account);
                jv[jss::OracleDocumentID] = 1;
                jv[jss::LastUpdateTime] = to_string(
                    duration_cast<seconds>(env.current()->header().closeTime.time_since_epoch())
                        .count() +
                    kEpochOffset.count() + 100);
                jv[jss::PriceDataSeries] = json::ValueType::Array;
                jv[jss::Provider] = strHex(std::string{"provider"});
                jv[jss::AssetClass] = strHex(std::string{"currency"});

                for (uint8_t i = 0; i < dataSeriesSize; ++i)
                {
                    json::Value row;
                    row[jss::PriceData][jss::BaseAsset] = "XRP";
                    row[jss::PriceData][jss::QuoteAsset] = "US" + std::to_string(i);
                    row[jss::PriceData][jss::AssetPrice] = to_string(740 + i);
                    row[jss::PriceData][jss::Scale] = 1;
                    jv[jss::PriceDataSeries].append(row);
                }

                return jv;
            };

            // Ensure a sponsorship object exists for (sponsor -> alice) with reserve count 2
            env(sponsor::set_reserve(sponsor, 0, 2), sponsor::SponseeAcc(alice), Ter(tesSUCCESS));
            env.close();

            // Step 1: create oracle with 6 pairs (reserve 2)
            submitSponsored(oracleSet(alice, 6), tesSUCCESS);

            // Verify oracle was created with 6 pairs
            {
                auto const sle = env.le(keylet::oracle(alice, 1));
                if (BEAST_EXPECT(sle))
                    BEAST_EXPECT(sle->getFieldArray(sfPriceDataSeries).size() == 6);
            }

            // Step 2: update to effective 5 pairs by deleting US5 (last pair)
            // This creates a reserve-decreasing update (6->5 pairs, reserve 2->1, adjustReserve=-1)
            // Without fix: Would fail with tecINSUFFICIENT_RESERVE due to signed/unsigned
            // comparison With fix (std::cmp_less): Should succeed with tesSUCCESS
            auto jv = oracleSet(alice, 5);

            // Add a delete row for US5 (no AssetPrice => delete pair)
            json::Value delPrice;
            delPrice[jss::BaseAsset] = "XRP";
            delPrice[jss::QuoteAsset] = "US5";
            json::Value delRow;
            delRow[jss::PriceData] = delPrice;  // no AssetPrice => delete pair
            jv[jss::PriceDataSeries].append(delRow);

            // This should succeed (and does with the fix)
            submitSponsored(jv, tesSUCCESS);

            // Verify the update succeeded and we now have 5 pairs
            {
                auto const sle = env.le(keylet::oracle(alice, 1));
                if (BEAST_EXPECT(sle))
                {
                    BEAST_EXPECT(sle->getFieldArray(sfPriceDataSeries).size() == 5);
                    // Also verify sponsorship is still in place
                    BEAST_EXPECT(sle->isFieldPresent(sfSponsor));
                }
            }
        }
    }

    void
    test1364AmmWithdrawSponsoredMptBypass()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1364

        // AMMWithdraw::equalWithdrawTokens() misses prefunded-sponsor reserve check,
        // allowing a sponsee to create a sponsored MPToken without consuming sfRemainingOwnerCount
        // when the sponsor's owner count is below 2.
        testcase("AMMWithdraw bypasses prefunded sponsor ReserveCount for MPT creation");

        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const sponsor{"sponsor"};

        // Fund all accounts with sufficient XRP
        env.fund(XRP(1000), issuer, alice, bob, sponsor);
        env.close();

        // Drain sponsor (just enough for a Fee-only sponsorship but not reserve
        // sponsorship)
        adjustAccountXRPBalance(
            env, sponsor, reserve(env, 1) + XRP(1) + env.current()->fees().base);
        // auto const sponsorSle = env.le(sponsor);
        // log << "Sponsor balance: " << sponsorSle->at(sfBalance) << std::endl;

        // Get initial sponsor state
        auto const sponsoringOwnerCountBefore = env.sponsoringOwnerCount(sponsor);

        // Step 1: Create MPT with lsfMPTCanTrade | lsfMPTCanTransfer flags. Both are needed for AMM
        // trading. MPTokenIssuanceCreate
        MPTTester mpt(
            {.env = env, .issuer = issuer, .flags = kMptDexFlags, .maxAmt = 1'000'000'000});
        env.close();

        // Step 2: MPTokenAuthorize and pay to alice
        mpt.authorize({.account = alice, .id = mpt.issuanceID()});
        env.close();
        env(pay(issuer, alice, mpt(20'000)));
        env.close();

        // Step 3: Alice creates an AMM pool with XRP and MPT, Fee = 2XRP
        AMM const amm(
            env, alice, XRP(10), mpt(10'000), false, 0, env.current()->fees().increment.drops());
        env.close();
        BEAST_EXPECT(amm.expectTradingFee(0));

        // Step 3: Bob deposits into AMM to get LP tokens, Bob needs LP tokens to be able to
        // withdraw
        auto const jv1 = amm.depositJv(
            {.account = bob, .asset1In = XRP(1), .flags = tfSingleAsset, .assets = {{XRP, mpt}}});
        // log << jv1.toStyledString() << std::endl;
        env(jv1);
        env.close();

        // Verify Bob does NOT have an MPToken yet
        BEAST_EXPECT(env.le(keylet::mptoken(mpt.issuanceID(), bob)) == nullptr);

        // Step 4: Sponsor creates a Fee-only sponsorship for Bob (no ReserveCount)
        // log << "Sponsor balance: " << sponsorSle->at(sfBalance) << std::endl;
        env(sponsor::set_fee(sponsor, 0, drops(1'000'000)), sponsor::SponseeAcc(bob));
        env.close();

        // Verify sponsorship exists with FeeAmount but no ReserveCount
        {
            auto const sle = env.le(keylet::sponsorship(sponsor, bob));
            BEAST_EXPECT(sle) && BEAST_EXPECT(sle->isFieldPresent(sfFeeAmount)) &&
                BEAST_EXPECT(!sle->isFieldPresent(sfRemainingOwnerCount));
        }

        // Control: the regular MPTokenAuthorize path correctly treats a prefunded
        // sponsor as requiring ReserveCount even while the sponsor's owner count is
        // below the free-object threshold.
        env(mpt.authorizeJV({.account = bob, .id = mpt.issuanceID()}),
            sponsor::As(sponsor, spfSponsorReserve),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // Step 6: Exploit - AMMWithdraw with prefunded sponsor succeeds
        // This should also fail with tecINSUFFICIENT_RESERVE but the bug allows it to succeed
        // Fixed, failed
        env(amm.withdrawJv(
                WithdrawArg{
                    .account = bob,
                    .asset1Out = mpt(1),
                    .flags = tfSingleAsset,
                    .assets = {{mpt, XRP}}}),
            sponsor::As(sponsor, spfSponsorReserve),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // Verify the bug: MPToken was created without consuming ReserveCount
        // FIXED
        auto const mptokenAfter = env.le(keylet::mptoken(mpt.issuanceID(), bob));
        BEAST_EXPECT(!mptokenAfter);
        // && BEAST_EXPECT(mptokenAfter->isFieldPresent(sfSponsor)) &&
        // BEAST_EXPECT(mptokenAfter->getAccountID(sfSponsor) == sponsor.id());

        // Verify sponsorship still has no ReserveCount
        {
            auto const sle = env.le(keylet::sponsorship(sponsor, bob));
            BEAST_EXPECT(sle) && BEAST_EXPECT(!sle->isFieldPresent(sfRemainingOwnerCount));
        }

        // Verify Bob's SponsoredOwnerCount increased
        // fixed
        {
            auto const bobAfter = env.le(keylet::account(bob));
            BEAST_EXPECT(bobAfter) &&
                BEAST_EXPECT(bobAfter->getFieldU32(sfSponsoredOwnerCount) == 0);
        }

        // Verify sponsor's SponsoringOwnerCount increased
        {
            auto const sponsorAfter = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorAfter) &&
                BEAST_EXPECT(
                    sponsorAfter->getFieldU32(sfSponsoringOwnerCount) ==
                    sponsoringOwnerCountBefore);

            // Verify sponsor is not under-reserved
            // Sponsor should need: base(10 XRP) + sponsoringOwnerCount(1) * inc(2 XRP) = 12 XRP
            auto const sponsorBalance = sponsorAfter->getFieldAmount(sfBalance);
            auto const requiredReserve = accountReserve(*env.current(), sponsorAfter, env.journal);
            BEAST_EXPECT(sponsorBalance >= requiredReserve);
        }
    }

    void
    test1380AmmClawbackReserveBypass()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1380
        // AMMWithdraw's sufficientReserve lambda was reading sponsor from tx instead of target
        // account This caused reserve check bypass during AMMClawback when issuer used a fee
        // sponsor FIX: Line 613-614 now checks tx[sfAccount] == account before using transaction
        // sponsor

        testcase("AMMClawback reserve bypass via fee sponsor (FIXED)");

        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const gw{"gateway"};
        Account const gw2{"gateway2"};
        Account const alice{"alice"};
        Account const feeSponsor{"feeSponsor"};

        // Fund accounts
        env.fund(XRP(10000), gw, gw2, alice, feeSponsor);
        env.close();

        // Enable clawback for gateway
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();

        auto const usd = gw["USD"];
        auto const eur = gw2["EUR"];

        // Gateway 1 trusts and receives EUR to fund the AMM pool
        env.trust(eur(10000), gw);
        env(pay(gw2, gw, eur(10000)));
        env.close();

        // Create AMM pool (USD/EUR)
        AMM amm(env, gw, usd(1000), eur(1000));
        env.close();

        // Alice makes single-asset deposit (USD only, no EUR trustline)
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(400)));
        env.close();

        amm.deposit(alice, usd(400));
        env.close();

        // Verify Alice does NOT have EUR trustline yet
        BEAST_EXPECT(!env.le(keylet::line(alice, eur.issue())));

        // Drain Alice's XRP to exact base reserve (so creating EUR trustline would under-reserve
        // her)
        auto const aliceLeBefore = env.le(alice);
        auto const currentOwnerCount = aliceLeBefore->getFieldU32(sfOwnerCount);
        auto const baseReserve = reserve(env, currentOwnerCount);
        auto const drainAmount = env.balance(alice) - baseReserve - XRPAmount(100);
        if (drainAmount > XRPAmount{0})
        {
            env(pay(alice, gw, drainAmount));
            env.close();
        }

        // Create minimal fee sponsor account with OwnerCount = 0 (< 2)
        // This is the key to the bug - fee sponsor with low owner count
        // Verify fee sponsor has OwnerCount < 2
        auto const sponsorSle = env.le(feeSponsor);
        BEAST_EXPECT(sponsorSle->getFieldU32(sfOwnerCount) == 0);

        // Create fee-only sponsorship (no reserve sponsorship)
        env(sponsor::set_fee(feeSponsor, 0, XRP(100)), sponsor::SponseeAcc(gw));
        env.close();

        // Verify sponsorship exists with FeeAmount only (no ReserveCount)
        {
            auto const sle = env.le(keylet::sponsorship(feeSponsor, gw));
            BEAST_EXPECT(
                sle && sle->isFieldPresent(sfFeeAmount) &&
                !sle->isFieldPresent(sfRemainingOwnerCount));
        }

        // Attempt AMMClawback with fee sponsor
        // Without fix: Would succeed and create EUR trustline for Alice without reserve check
        // With fix: Should fail with tecINSUFFICIENT_RESERVE because Alice lacks reserve
        env(amm::ammClawback(gw, alice, usd, eur, std::nullopt),
            sponsor::As(feeSponsor, spfSponsorFee),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // Verify EUR trustline was NOT created for Alice (fix working)
        BEAST_EXPECT(!env.le(keylet::line(alice, eur.issue())));

        // Verify Alice's owner count didn't increase
        auto const aliceLeAfter = env.le(alice);
        BEAST_EXPECT(aliceLeAfter->getFieldU32(sfOwnerCount) == currentOwnerCount);

        // Verify Alice is not under-reserved
        auto const reserveAfter = reserve(env, aliceLeAfter->getFieldU32(sfOwnerCount));
        BEAST_EXPECT(env.balance(alice) >= reserveAfter);
    }

    void
    test1468PathPaymentExploit()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1468
        // This test demonstrates the FULL EXPLOITATION of the bug via path payment
        //
        // BUG PROOF: The test triggers an assertion failure in StrandFlow.h:192
        // This assertion catches the inconsistency: During reverse pass calculation,
        // sfSponsoringOwnerCount changes from 3->0, causing liquid XRP calculation to
        // change from 2 XRP -> 8 XRP mid-transaction. When StrandFlow re-executes the
        // limiting step for verification, it gets a different result, proving the bug.
        //
        // NOTE: This test will ABORT in debug builds due to assertion failure.
        // This is EXPECTED and PROVES the bug. To run other tests, comment this test out.
        // In production builds (without assertions), this would cause actual reserve bypass.

        testcase("PaymentSandbox sfSponsoringOwnerCount bypass - PATH PAYMENT EXPLOIT");

        using namespace test::jtx;

        auto cfg = makeConfig();
        cfg->fees.referenceFee = 12;
        cfg->fees.accountReserve = XRP(10).value().xrp();
        cfg->fees.ownerReserve = XRP(2).value().xrp();
        Env env{*this, std::move(cfg), testableAmendments()};

        Account const sponsor{"sponsor"};
        Account const sponsee{"sponsee"};
        Account const issuer{"issuer"};
        Account const dest{"dest"};

        // Fund accounts
        env.fund(XRP(500), sponsor, sponsee, issuer, dest);
        env.close();

        // Setup trustlines
        auto const usd = issuer["USD"];
        env.trust(usd(10'000), sponsee);
        env.trust(usd(10'000), dest);
        env.close();

        // Issuer pays USD to sponsee
        env(pay(issuer, sponsee, usd(1'000)));
        env.close();

        // Sponsor creates sponsorship with ReserveCount=5 for sponsee
        env(sponsor::set_reserve(sponsor, 0, 5), sponsor::SponseeAcc(sponsee));
        env.close();

        // Sponsee creates 3 sponsored offers: XRP -> USD
        // These will be crossed by the path payment
        for (int i = 0; i < 3; ++i)
        {
            env(offer(sponsee, XRP(50), usd(100)), sponsor::As(sponsor, spfSponsorReserve));
            env.close();
        }

        // Create 7 trustlines for sponsor to increase sfOwnerCount to 8
        for (int i = 0; i < 7; ++i)
        {
            auto const tempIssuer = Account("tempIssuer" + std::to_string(i));
            env.fund(XRP(30), tempIssuer);
            env.close();
            auto const aaa = tempIssuer["AAA"];
            env.trust(aaa(1), sponsor);
            env.close();
        }

        // Log sponsor state BEFORE adjustment
        auto const sponsorSle1 = env.le(sponsor);
        auto const reserve1 = accountReserve(*env.current(), sponsorSle1, env.journal);
        // auto const liquid1 = xrpLiquid(*env.current(), sponsorSle1, 0, env.journal);
        auto const ownerCount1 = sponsorSle1->getFieldU32(sfOwnerCount);
        auto const sponsoringOC1 = sponsorSle1->getFieldU32(sfSponsoringOwnerCount);

        BEAST_EXPECT(ownerCount1 == 8);    // 7 trustlines + 1 sponsorship
        BEAST_EXPECT(sponsoringOC1 == 3);  // 3 sponsored offers
        // Reserve should be: 10 + 2*(8+3) = 32 XRP
        BEAST_EXPECT(reserve1 == XRP(32));

        // CRITICAL: Adjust sponsor balance to exactly 34 XRP
        // This sets liquid = 34 - 32 = 2 XRP
        adjustAccountXRPBalance(env, sponsor, XRP(34));

        // Verify the setup
        auto const sponsorSle2 = env.le(sponsor);
        auto const balance2 = sponsorSle2->at(sfBalance);
        auto const reserve2 = accountReserve(*env.current(), sponsorSle2, env.journal);
        auto const liquid2 = xrpLiquid(*env.current(), sponsorSle2, 0, env.journal);

        BEAST_EXPECT(balance2 == XRP(34));
        BEAST_EXPECT(reserve2 == XRP(32));
        BEAST_EXPECT(liquid2 == XRP(2));

        // "=== EXPLOITATION: Path Payment ==="
        // "Sending path payment: sponsor -> dest" << std::endl;
        // "Path: XRP -> [BookStep crosses sponsored offers] -> USD" << std::endl;
        // "Expected: StrandFlow reverse pass will:" << std::endl;
        // "  1. BookStep deletes offers, drops sfSponsoringOwnerCount 3->0" << std::endl;
        // "  2. XRPEndpointStep sees sfSponsoringOwnerCount=0 (BUG!)" << std::endl;
        // "  3. Calculates liquid = 34 - 26 = 8 XRP (should be 2 XRP!)" << std::endl;
        // "  4. This inconsistency causes assertion failure in StrandFlow" << std::endl;
        // "  5. OR allows sending MORE than 2 XRP" << std::endl;

        // EXPLOITATION: Path payment from sponsor (XRP) to dest (USD)
        // The path will cross the sponsored offers via BookStep
        // tfNoRippleDirect forces use of the path
        // tfPartialPayment allows partial delivery
        // NOTE: This may trigger an assertion in StrandFlow due to the bug!
        // The assertion proves inconsistency in reserve calculations

        env(pay(sponsor, dest, usd(250)),
            Sendmax(XRP(200)),
            Path(~usd),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        // Log sponsor state AFTER exploitation
        auto const sponsorSle3 = env.le(sponsor);
        auto const balance3 = sponsorSle3->at(sfBalance);
        auto const reserve3 = accountReserve(*env.current(), sponsorSle3, env.journal);
        auto const liquid3 = xrpLiquid(*env.current(), sponsorSle3, 0, env.journal);
        auto const ownerCount3 = sponsorSle3->getFieldU32(sfOwnerCount);
        auto const sponsoringOC3 = sponsorSle3->getFieldU32(sfSponsoringOwnerCount);

        // Calculate XRP spent
        auto const xrpSpent = balance2.xrp() - balance3.xrp();

        // BUG CONFIRMED: sfSponsoringOwnerCount dropped from 3 to 0
        // BEAST_EXPECT(sponsoringOC3 == 0);
        BEAST_EXPECT(sponsoringOC3 == sponsoringOC1);

        // Reserve calculation after: 10 + 2*8 = 26 XRP (3 sponsored offers deleted)
        //  BEAST_EXPECT(reserve3 == XRP(26));
        BEAST_EXPECT(reserve3 == reserve1);

        // EXPLOITATION CONFIRMED: Sponsor spent MORE than the legitimate 2 XRP liquid
        // BEAST_EXPECT(xrpSpent > XRP(2));
        BEAST_EXPECT(xrpSpent <= XRP(2));

        // EXPLOITATION CONFIRMED: balance under reserve
        // BEAST_EXPECT(balance3 < reserve3);
        BEAST_EXPECT(balance3 >= reserve3);
        BEAST_EXPECT(!liquid3.negative());
        BEAST_EXPECT(ownerCount3 == 8);
    }

    void
    test1563OracleIncorrectAdjustment()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1563
        //
        // BUG: Oracle updates with reserve decrease (2->1) failed with tecINSUFFICIENT_RESERVE
        // when sponsor fields were omitted from the transaction, even though the sponsor
        // was not changing.
        //
        // ROOT CAUSE: OracleSet::preclaim treated omitted sponsor fields as "sponsor removal"
        // instead of "keep existing sponsor", causing it to check for full new reserve instead
        // of delta reserve adjustment.
        //
        // FIX: The logic now checks:
        // - If reserve INCREASES and sponsor changes: charge full new reserve (sponsor must
        // consent)
        // - If reserve DECREASES or stays same: charge delta reserve, keep existing sponsor
        //
        // NEW RULE: Sponsor can only change when reserve requirement increases.

        testcase("Oracle reserve decrease keeps sponsor (bug 1563)");

        using namespace test::jtx;
        using namespace std::chrono;

        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const oracleSet =
            [](Env& env, Account const& account, uint8_t newCnt, uint8_t oldCnt = 0) {
                auto const now = env.timeKeeper().now();
                env.close(now + oracle::kTestStartTime - kEpochOffset);
                json::Value jv;
                jv[jss::TransactionType] = jss::OracleSet;
                jv[jss::Account] = to_string(account);
                jv[jss::OracleDocumentID] = 1;
                jv[jss::LastUpdateTime] = to_string(
                    duration_cast<seconds>(env.current()->header().closeTime.time_since_epoch())
                        .count() +
                    kEpochOffset.count() + 100);
                jv[jss::PriceDataSeries] = json::ValueType::Array;
                jv[jss::Provider] = strHex(std::string{"provider"});
                jv[jss::AssetClass] = strHex(std::string{"currency"});

                using DataSeries =
                    std::vector<std::tuple<std::string, std::string, std::uint32_t, std::uint8_t>>;
                DataSeries const series = {
                    {"XRP", "US1", 740, 1},
                    {"XRP", "US2", 750, 1},
                    {"XRP", "US3", 740, 1},
                    {"XRP", "US4", 750, 1},
                    {"XRP", "US5", 740, 1},
                    {"XRP", "US6", 750, 1},
                    {"XRP", "US7", 740, 1},
                    {"XRP", "US8", 750, 1},
                    {"XRP", "US9", 740, 1},
                    {"XRP", "U10", 750, 1},
                };

                json::Value dataSeries(json::ValueType::Array);
                DataSeries const actualSeries(
                    series.begin(), series.begin() + std::max(newCnt, oldCnt));
                uint8_t i = 0;
                for (auto const& data : actualSeries)
                {
                    json::Value priceData;
                    json::Value price;
                    price[jss::BaseAsset] = std::get<0>(data);
                    price[jss::QuoteAsset] = std::get<1>(data);
                    if (i++ < newCnt)
                        price[jss::AssetPrice] = std::get<2>(data);
                    price[jss::Scale] = std::get<3>(data);
                    priceData[jss::PriceData] = price;
                    dataSeries.append(priceData);
                }
                jv[jss::PriceDataSeries] = dataSeries;
                return jv;
            };

        auto const oracleDelete = [&](Account const& account, unsigned id = 1) {
            json::Value jv;
            jv[jss::TransactionType] = jss::OracleDelete;
            jv[jss::Account] = to_string(account);
            jv[jss::OracleDocumentID] = id;
            return jv;
        };

        Env env{*this, testableAmendments()};
        env.fund(XRP(1000000), alice, sponsor, sponsor2);
        env.close();

        // Test 1: Reserve decrease 2->1 with sponsored oracle (OMIT sponsor fields)
        // This was the ORIGINAL BUG - should succeed but failed with tecINSUFFICIENT_RESERVE
        {
            // Create sponsored oracle with 6 price pairs (reserve = 2)
            env(oracleSet(env, alice, 6),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            // Verify oracle has sponsor field
            auto const oracleSle = env.le(keylet::oracle(alice.id(), 1));
            if (!BEAST_EXPECT(oracleSle))
                return;
            BEAST_EXPECT(oracleSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(oracleSle->getAccountID(sfSponsor) == sponsor.id());

            // Update oracle to 5 pairs (reserve 2->1) WITHOUT sponsor fields
            // BUG: This used to fail with tecINSUFFICIENT_RESERVE
            // FIX: Should succeed - sponsor is kept, only delta adjustment
            env(oracleSet(env, alice, 5, 6));  // No sponsor fields!
            env.close();

            // Verify reserve decreased to 1
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // CRITICAL: Verify sponsor DID NOT CHANGE
            auto const oracleSle2 = env.le(keylet::oracle(alice.id(), 1));
            BEAST_EXPECT(oracleSle2->isFieldPresent(sfSponsor));
            BEAST_EXPECT(oracleSle2->getAccountID(sfSponsor) == sponsor.id());

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }

        // Test 2: Reserve stays same (1->1) with sponsored oracle (OMIT sponsor fields)
        {
            // Create sponsored oracle with 5 price pairs (reserve = 1)
            env(oracleSet(env, alice, 5),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Update to 4 pairs (reserve stays 1->1) WITHOUT sponsor fields
            env(oracleSet(env, alice, 4, 5));  // No sponsor fields!
            env.close();

            // Verify reserve stayed at 1
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Verify sponsor unchanged
            auto const oracleSle = env.le(keylet::oracle(alice.id(), 1));
            BEAST_EXPECT(oracleSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(oracleSle->getAccountID(sfSponsor) == sponsor.id());

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }

        // Test 3: Reserve increase 1->2 WITHOUT explicit sponsor change
        // Should fail - cannot keep sponsor on increase without explicit consent
        {
            // Create sponsored oracle with 5 price pairs (reserve = 1)
            env(oracleSet(env, alice, 5),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Reduce alice's balance so she can't afford the 1 reserve increase (2 XRP)
            // Set her balance to just above base reserve (10 XRP) + small buffer
            adjustAccountXRPBalance(env, alice, reserve(env, 0) + XRP(1));

            // Try to update to 6 pairs (reserve 1->2) WITHOUT sponsor fields
            // Should fail - alice doesn't have enough XRP to cover the increase
            env(oracleSet(env, alice, 6), Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            // Verify oracle unchanged
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }

        // Test 4: Reserve increase 1->2 WITH explicit sponsor (same sponsor)
        // Should succeed - sponsor consents to increase
        {
            adjustAccountXRPBalance(env, alice, XRP(1000));
            // Create sponsored oracle with 5 price pairs (reserve = 1)
            env(oracleSet(env, alice, 5),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Update to 6 pairs (reserve 1->2) WITH same sponsor
            env(oracleSet(env, alice, 6),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            // Verify reserve increased to 2
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            // Verify sponsor unchanged
            auto const oracleSle = env.le(keylet::oracle(alice.id(), 1));
            BEAST_EXPECT(oracleSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(oracleSle->getAccountID(sfSponsor) == sponsor.id());

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }

        // Test 5: Reserve increase 1->2 WITH explicit sponsor CHANGE
        // Should succeed - new sponsor takes over
        {
            // Create sponsored oracle with 5 price pairs (reserve = 1)
            env(oracleSet(env, alice, 5),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

            // Update to 6 pairs (reserve 1->2) WITH different sponsor
            env(oracleSet(env, alice, 6),
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2));
            env.close();

            // Verify reserve increased to 2
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);

            // CRITICAL: Verify sponsor CHANGED from sponsor to sponsor2
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);   // old sponsor released
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);  // new sponsor took over

            auto const oracleSle = env.le(keylet::oracle(alice.id(), 1));
            BEAST_EXPECT(oracleSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(oracleSle->getAccountID(sfSponsor) == sponsor2.id());

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }

        // Test 6: Multiple reserve changes with sponsor
        {
            // Create with 6 pairs (reserve = 2)
            env(oracleSet(env, alice, 6),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            // Decrease 2->1 (no sponsor fields)
            env(oracleSet(env, alice, 5, 6));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Increase back 1->2 (with sponsor consent)
            env(oracleSet(env, alice, 6),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            // Decrease 2->1 again (no sponsor fields)
            env(oracleSet(env, alice, 3, 6));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Verify sponsor never changed
            auto const oracleSle = env.le(keylet::oracle(alice.id(), 1));
            BEAST_EXPECT(oracleSle->getAccountID(sfSponsor) == sponsor.id());

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }

        // Test 7: Un-sponsored oracle - decrease should still work
        {
            // Create un-sponsored oracle with 6 pairs (reserve = 2)
            env(oracleSet(env, alice, 6));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);

            // Decrease to 5 pairs (2->1)
            env(oracleSet(env, alice, 5, 6));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);

            // Verify no sponsor field
            auto const oracleSle = env.le(keylet::oracle(alice, 1));
            BEAST_EXPECT(!oracleSle->isFieldPresent(sfSponsor));

            // Cleanup
            env(oracleDelete(alice));
            env.close();
        }
    }

    void
    test1675SponsoredXRPEscrowCreate()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1675
        //
        // BUG: Sponsored XRP EscrowCreate failed with tecUNFUNDED for low-balance senders
        // because the second post-amount reserve check hard-coded sponsor = {}, routing
        // through the un-sponsored branch and requiring the sender to self-fund the reserve.
        //
        // ROOT CAUSE: The second checkInsufficientReserve() call passed sponsor = {}
        // instead of the actual sponsor, causing it to check un-sponsored reserve requirements.
        //
        // FIX: The code was refactored to use a single checkXrpBalance() call with:
        // - sponsorSle correctly set to getTxReserveSponsor(view(), ctx_.tx)
        // - balanceAdj set to -amount.xrp() for XRP escrows
        // This properly accounts for both the sponsor and the amount being escrowed.

        testcase("Sponsored XRP EscrowCreate with low-balance sender (bug 1675)");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Env env{*this, testableAmendments()};

        Account const sender("sender");
        Account const sponsor("sponsor");
        Account const dst("dst");

        // Fund sender with 260 XRP - just enough for base reserve (200 XRP) + 50 XRP escrow + 10
        // XRP buffer This is the boundary case: sender can afford the escrow amount but NOT the
        // additional reserve increment
        env.fund(XRP(260), sender);
        env.fund(XRP(2000), sponsor);
        env.fund(XRP(200), dst);
        env.close();

        // Verify initial balances
        BEAST_EXPECT(env.balance(sender) == XRP(260));
        BEAST_EXPECT(env.balance(sponsor) == XRP(2000));
        BEAST_EXPECT(ownerCount(env, sender) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, sender) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

        // Create sponsored XRP escrow
        // With the bug: this would fail with tecUNFUNDED because the second reserve check
        // requires sender to have 250 XRP (200 base + 50 reserve increment), but sender only has
        // 210 XRP after escrow With the fix: this should succeed because the sponsor covers the
        // reserve increment
        env(escrow::create(sender, dst, XRP(50)),
            escrow::kFinishTime(env.now() + 10s),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();

        // Verify escrow was created successfully
        BEAST_EXPECT(ownerCount(env, sender) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, sender) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // Verify sender's balance decreased by the escrow amount (50 XRP) + fee
        auto const fee = env.current()->fees().base;
        BEAST_EXPECT(env.balance(sender) == XRP(260) - XRP(50) - fee);

        // Verify the escrow object exists and is sponsored
        auto const escrowKey = keylet::escrow(sender.id(), env.seq(sender) - 1);
        auto const escrowSle = env.le(escrowKey);
        if (BEAST_EXPECT(escrowSle))
        {
            BEAST_EXPECT(escrowSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(escrowSle->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT((*escrowSle)[sfAmount] == XRP(50));
        }
    }

    void
    test1678SameSponsorCredentialAccept()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1678
        //
        // BUG: Same-sponsor CredentialAccept fails with tecINSUFFICIENT_RESERVE for sponsor
        // at reserve floor because the reserve check fires BEFORE the old sponsorship is released.
        //
        // ROOT CAUSE: CredentialAccept::doApply() calls checkXrpBalance(+1) for the new
        // subject-side sponsorship BEFORE releasing the old issuer-side sponsorship at line 119.
        // This causes a transient double-count: the sponsor needs current + 1 momentarily,
        // even though the final state is reserve-neutral (same sponsor, net zero change).
        //
        // EXPECTED FIX: Either reorder to release old sponsorship first, OR detect same-sponsor
        // case and skip the transient +1 check.

        testcase("Same-sponsor CredentialAccept reserve handoff (bug 1678)");

        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const issuer("issuer");
        Account const subject("subject");
        Account const sponsor("sponsor");
        std::string const credType = "TEST";

        // Fund sponsor with exactly 280 XRP - enough for 1 sponsored credential
        // (200 XRP base reserve + 50 XRP owner reserve increment + 30 XRP buffer)
        // This is at the threshold: sponsor can afford 1 sponsored object but not transiently 2
        env.fund(XRP(280), sponsor);
        env.fund(XRP(10000), issuer, subject);
        env.close();

        // Verify initial balances
        BEAST_EXPECT(env.balance(sponsor) == XRP(280));
        BEAST_EXPECT(ownerCount(env, issuer) == 0);
        BEAST_EXPECT(ownerCount(env, subject) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

        // Step 1: Issuer creates a credential for subject with sponsor's reserve sponsorship
        // After this, the credential is owned by the issuer (issuer-side)
        env(credentials::create(subject, issuer, credType),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();

        // Verify credential was created and sponsored
        BEAST_EXPECT(ownerCount(env, issuer) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        auto const credKey = credentials::keylet(subject, issuer, credType);
        auto const credSle = env.le(credKey);
        if (BEAST_EXPECT(credSle))
        {
            BEAST_EXPECT(credSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(credSle->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(!credSle->isFlag(lsfAccepted));
        }

        // Step 2: Subject accepts the credential with SAME sponsor
        // This should succeed (final state is reserve-neutral: sponsor still sponsors 1 object)
        // After fix, old sponsorship is released BEFORE reserve check, avoiding transient
        // double-count
        env(credentials::accept(subject, issuer, credType),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();

        // VERIFICATION: Credential has been accepted successfully
        auto const credSle2 = env.le(credKey);
        if (BEAST_EXPECT(credSle2))
        {
            // Credential is accepted (lsfAccepted flag is set)
            BEAST_EXPECT(credSle2->isFlag(lsfAccepted));

            // Ownership has changed (now owned by subject, not issuer)
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 1);

            // Sponsorship is maintained (still 1 sponsored object by same sponsor)
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Verify the sponsor field is still set correctly
            BEAST_EXPECT(credSle2->isFieldPresent(sfSponsor));
            BEAST_EXPECT(credSle2->getAccountID(sfSponsor) == sponsor.id());
        }
    }

    void
    test1680SponsoredPayChanTrapsReserve()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1680
        //
        // CLARIFICATION: This is NOT a bug - it's DESIGNED BEHAVIOR per XLS-86 spec.
        //
        // Per XLS-86 section 10.5 (Failure Conditions for SponsorshipTransfer):
        // "If dissolving the sponsorship (no Sponsor field or spfSponsorReserve flag not set):
        //  The owner does not have enough XRP to cover the reserve for this object/account
        //  (tecINSUFFICIENT_RESERVE)"
        //
        // DESIGNED BEHAVIOR:
        // - Sponsorship is a mutual relationship between sponsor and sponsee
        // - Either party can end the sponsorship IF the sponsee has sufficient funds to self-fund
        // - If the sponsee doesn't have sufficient funds, the sponsorship is "sticky" and requires
        // coordination
        // - This protects against malicious or accidental de-sponsoring that would violate reserve
        // requirements
        //
        // SCENARIO EXPLAINED:
        // 1. Sponsor pre-funds 1 reserve credit for sponsee
        // 2. Sponsee creates a PaymentChannel, locking XRP such that: balance - amount < base +
        // increment
        // 3. Channel creation succeeds (sponsor covers the reserve increment, sponsee has liquid
        // funds for amount)
        // 4. Sponsor tries to end sponsorship -> CORRECTLY FAILS with tecINSUFFICIENT_RESERVE
        //    (because sponsee cannot self-fund the reserve after locking the channel amount)
        // 5. The sponsorship remains in place until:
        //    a) Sponsee closes the channel and recovers the locked XRP, OR
        //    b) Sponsee adds more XRP to meet the reserve requirement, OR
        //    c) Sponsee transfers sponsorship to a new sponsor (tfSponsorshipReassign)
        //
        // This is CORRECT behavior - it prevents the ledger from entering an invalid state where
        // an account has objects but insufficient reserve to maintain them.

        testcase(
            "Sponsored PaymentChannelCreate with sticky sponsorship (designed behavior, not bug "
            "1680)");

        using namespace test::jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, testableAmendments()};

        Account const attackerA("attackerA");
        Account const attackerB("attackerB");
        Account const sponsor("sponsor");

        // Fund attackerA with 220 XRP - only 20 XRP above base reserve (200 XRP)
        // After creating channel with 15 XRP: balance = 205 XRP
        // But un-sponsored reserve floor would be: 200 base + 50 increment = 250 XRP
        // So A would be 45 XRP below un-sponsored floor
        env.fund(XRP(220), attackerA);
        env.fund(XRP(2000), sponsor);
        env.fund(XRP(200), attackerB);
        env.close();

        // Sponsor pre-funds 1 reserve credit for attackerA
        auto const sponsorBalanceBeforeSet = env.balance(sponsor);
        env(sponsor::set(sponsor, 0, 1), sponsor::SponseeAcc(attackerA), Ter(tesSUCCESS));
        env.close();

        // Verify initial state
        BEAST_EXPECT(env.balance(attackerA) == XRP(220));
        // Sponsor paid a fee for the SponsorshipSet transaction
        auto const setFee = env.current()->fees().base;
        BEAST_EXPECT(env.balance(sponsor) == sponsorBalanceBeforeSet - setFee);
        BEAST_EXPECT(ownerCount(env, attackerA) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, attackerA) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

        // Create a sponsored payment channel
        // This succeeds because:
        // - Sponsor has 1 reserve credit available (covers the 50 XRP reserve increment)
        // - AttackerA has liquid balance: 220 - 200 (base reserve) = 20 XRP
        // - After locking 15 XRP: 20 - 15 = 5 XRP liquid (still positive, sufficient for the
        // operation)
        //
        // The reserve check validates:
        // 1. Sponsor has reserve capacity for the new object (YES: 1 credit available)
        // 2. Source has liquid funds after locking the channel amount (YES: 5 XRP remaining)
        auto const channelKey = paychan::channel(attackerA, attackerB, env.seq(attackerA));
        env(paychan::create(attackerA, attackerB, XRP(15), 1s, attackerA.pk()),
            sponsor::As(sponsor, spfSponsorReserve),
            Ter(tesSUCCESS));
        env.close();

        // Verify the channel was created and is sponsored
        BEAST_EXPECT(paychan::channelExists(*env.current(), channelKey));
        BEAST_EXPECT(ownerCount(env, attackerA) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, attackerA) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // Verify attackerA's balance after locking the amount
        auto const fee = env.current()->fees().base;
        auto const expectedBalance = XRP(220) - XRP(15) - fee;
        BEAST_EXPECT(env.balance(attackerA) == expectedBalance);  // Should be ~205 XRP

        // DESIGNED BEHAVIOR: Sponsor tries to end the sponsorship via SponsorshipTransfer
        // This CORRECTLY fails with tecINSUFFICIENT_RESERVE because:
        // - AttackerA's balance: ~205 XRP
        // - Un-sponsored reserve requirement: 200 (base) + 50 (increment) = 250 XRP
        // - AttackerA is ~45 XRP short of self-funding the reserve!
        //
        // Per XLS-86 spec, SponsorshipTransfer with tfSponsorshipEnd MUST verify that
        // the sponsee can afford the reserve before allowing the sponsorship to end.
        // This prevents the ledger from entering an invalid state.
        env(sponsor::transfer(sponsor, tfSponsorshipEnd, channelKey),
            sponsor::SponseeAcc(attackerA),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // Verify the sponsorship remains in place (this is CORRECT behavior)
        BEAST_EXPECT(sponsoredOwnerCount(env, attackerA) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // Verify the channel still exists and is still sponsored
        auto const chanSle =
            env.le(keylet::payChan(attackerA.id(), attackerB.id(), env.seq(attackerA) - 1));
        if (BEAST_EXPECT(chanSle))
        {
            BEAST_EXPECT(chanSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(chanSle->getAccountID(sfSponsor) == sponsor.id());
        }

        // DESIGNED BEHAVIOR: The sponsorship is "sticky" until one of these occurs:
        // 1. AttackerA closes the channel (recovers 15 XRP) and then has enough to self-fund
        // 2. AttackerA receives more XRP to meet the reserve requirement
        // 3. AttackerA transfers the sponsorship to a new sponsor via tfSponsorshipReassign
        //
        // This is intentional protection against violating reserve requirements.
        // The sponsor entered this relationship voluntarily by pre-funding the reserve credit.
        // Both parties must coordinate to dissolve the sponsorship in a way that maintains
        // ledger validity (i.e., the sponsee must have sufficient funds to self-fund).
    }

    void
    test1736CrossCurrencyTfSponsorCreatedAccountBypassesReserve()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1736
        //
        // BUG (FIXED): Cross-currency tfSponsorCreatedAccount payments used to bypass sponsor
        // reserve check.
        //
        // ROOT CAUSE: Payment::doApply() created the sponsored AccountRoot and incremented
        // sfSponsoringAccountCount BEFORE deciding whether to use the direct-XRP branch or
        // the RippleCalc path-payment branch. The sponsor's XRP reserve check existed ONLY
        // in the direct-XRP branch. When a non-XRP sfSendMax was present, doApply() computed
        // ripple == true and entered RippleCalc, which returned without ever reaching the
        // reserve check.
        //
        // FIX: Added universal reserve check at Payment.cpp#L496-L511 that executes AFTER
        // account creation but BEFORE the direct/path branch decision. This check calls
        // checkXrpBalance() which validates the sponsor's reserve capacity for both
        // direct-XRP and cross-currency sponsored account creation, returning
        // tecNO_DST_INSUF_XRP when the sponsor lacks sufficient reserves.
        //
        // VERIFICATION: This test confirms the fix works correctly by attempting to create
        // a sponsored account via cross-currency payment when the sponsor lacks sufficient
        // reserves. The transaction now properly fails with tecNO_DST_INSUF_XRP.

        // THE LATEST UPDATE: cross currency sponsoring account not allowed

        testcase("Cross-currency tfSponsorCreatedAccount reserve check (bug 1736, not allowed)");

        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const gw{"gateway"};
        Account const sponsor{"sponsor"};
        Account const maker{"maker"};
        Account const newAccount{"newAccount"};
        auto const usd = gw["USD"];

        env.fund(XRP(10'000), gw, sponsor, maker);
        env.close();

        env.trust(usd(100'000), sponsor, maker);
        env.close();

        env(pay(gw, sponsor, usd(10'000)));
        env.close();

        // Maker offers XRP for USD so sponsor can route cross-currency.
        env(offer(maker, usd(10'000), XRP(10'000)));
        env.close();

        auto const fee = env.current()->fees().base;
        auto sponsorSle = env.le(keylet::account(sponsor));
        if (!BEAST_EXPECT(sponsorSle))
            return;
        auto reserve = accountReserve(*env.current(), sponsorSle, env.journal);

        // Set XRP that is NOT enough to sponsor additional account
        adjustAccountXRPBalance(env, sponsor, STAmount(reserve + (fee * 20)));

        // EXPECTED BEHAVIOR (POST-FIX): Attempt to create a sponsored account with 1 XRP
        // XRP delivered via cross-currency path (USD -> XRP through DEX).
        // The universal reserve check at Payment.cpp#L496-L511 now validates that the sponsor
        // has sufficient reserves BEFORE committing the transaction, regardless of whether
        // it's a direct-XRP or cross-currency payment.
        env(pay(sponsor, newAccount, XRP(1)),
            Sendmax(usd(1)),
            Txflags(tfSponsorCreatedAccount),
            Ter(temINVALID));  // NOT ALLOWED
        env.close();
    }

    void
    test1779BrokerSponsorMisroutedToBorrowerLoanSle()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1779
        testcase("LoanSet does not misroute broker's tx sponsor onto borrower's Loan SLE");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const issuer{"issuer"};
        Account const lender{"lender"};      // broker owner / submitter
        Account const borrower{"borrower"};  // counterparty
        Account const sp{"sponsor"};         // lender's sponsor

        // Fund everyone amply.
        env.fund(XRP(100000), issuer, lender, borrower, sp);
        env.close();

        // Use an IOU asset for the vault.
        auto const iou = issuer["IOU"];
        env(trust(lender, iou(100'000'000)));
        env(trust(borrower, iou(100'000'000)));
        env.close();
        env(pay(issuer, lender, iou(1'000'000)));
        env.close();

        // 1. Create the vault, deposit the principal supply.
        Vault const vault{env};
        auto [vaultTx, vaultKeylet] = vault.create({.owner = lender, .asset = iou.issue()});
        env(vaultTx);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = iou(500'000)}));
        env.close();

        // 2. Create the LoanBroker with minimal required fields.
        auto const brokerKeylet = keylet::loanbroker(lender.id(), env.seq(lender));
        env(loanBroker::set(lender, vaultKeylet.key, 0),
            loanBroker::kManagementFeeRate(TenthBips16{100}),
            loanBroker::kDebtMaximum(iou(100'000).number()),
            loanBroker::kCoverRateMinimum(TenthBips32{1000}),
            loanBroker::kCoverRateLiquidation(TenthBips32{2500}));
        env.close();
        BEAST_EXPECT(env.le(brokerKeylet));

        // Cover deposit.
        env(loanBroker::coverDeposit(lender, brokerKeylet.key, iou(2'000)));
        env.close();

        // Capture state BEFORE the LoanSet.
        auto const borrowerOwnerCountBefore =
            env.le(keylet::account(borrower))->getFieldU32(sfOwnerCount);
        auto const borrowerSponsoredBefore =
            env.le(keylet::account(borrower))->at(~sfSponsoredOwnerCount).value_or(0);
        auto const sponsorSponsoringBefore =
            env.le(keylet::account(sp))->at(~sfSponsoringOwnerCount).value_or(0);

        // 4. The lender (broker owner) submits the LoanSet with a
        //    co-signing tx-level reserve sponsor = sp. Borrower
        //    co-signs via sfCounterpartySignature.
        Number const principal = iou(1'000).number();
        auto const loanKeylet =
            keylet::loan(brokerKeylet.key, env.le(brokerKeylet)->getFieldU32(sfLoanSequence));

        env(loan::set(lender, brokerKeylet.key, principal),
            loan::kCounterparty(borrower),
            Sig(sfCounterpartySignature, borrower),
            Fee(env.current()->fees().base * 4),
            sponsor::As(sp, spfSponsorReserve),
            Sig(sfSponsorSignature, sp),
            Ter(tesSUCCESS));
        env.close();

        // 5. Verify the FIX:
        //    a) Loan SLE exists and belongs to the borrower.
        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        if (loanSle)
            BEAST_EXPECT(loanSle->getAccountID(sfBorrower) == borrower.id());

        //    b) Loan SLE should NOT have sfSponsor = sp because the broker
        //       submitted the transaction, not the borrower.
        //       The fix checks if account_ == borrower before applying sponsor.
        if (loanSle)
        {
            BEAST_EXPECT(!loanSle->isFieldPresent(sfSponsor));
        }

        //    c) Borrower's sfSponsoredOwnerCount should NOT have grown
        //       because the sponsor was not applied.
        auto const borrowerSponsoredAfter =
            env.le(keylet::account(borrower))->at(~sfSponsoredOwnerCount).value_or(0);
        BEAST_EXPECT(borrowerSponsoredAfter == borrowerSponsoredBefore);

        //    d) Sp's sfSponsoringOwnerCount should NOT have grown
        //       because the sponsor was not applied to the borrower's loan.
        auto const sponsorSponsoringAfter =
            env.le(keylet::account(sp))->at(~sfSponsoringOwnerCount).value_or(0);
        BEAST_EXPECT(sponsorSponsoringAfter == sponsorSponsoringBefore);

        //    e) Borrower's regular OwnerCount should have grown by 1
        //       (the loan is their responsibility).
        auto const borrowerOwnerCountAfter =
            env.le(keylet::account(borrower))->getFieldU32(sfOwnerCount);
        BEAST_EXPECT(borrowerOwnerCountAfter == borrowerOwnerCountBefore + 1);
    }

    void
    test1814AMMDepositLPTokenNonSponsoredReserveBypass()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1814
        // Bug: AMMDeposit skips LP trustline reserve for non-sponsored users
        // The sponsor integration changed doApply balance check in AMMDeposit::deposit()
        // to incorrectly discard the LP trustline reserve adjustment for non-sponsored deposits.
        // This test verifies the bug is FIXED.

        testcase(
            "First-time LP tfLPToken deposit correctly enforces LP trustline reserve (1814 fix)");

        using namespace test::jtx;

        auto const features =
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
        Env env{*this, features};

        Account const gw("gw");
        Account const poolCreator("poolCreator");
        Account const alice("alice");

        auto const usd = gw["USD"];

        env.fund(XRP(100'000), gw, poolCreator);
        env.close();
        env(trust(poolCreator, usd(100'000)));
        env.close();
        env(pay(gw, poolCreator, usd(50'000)));
        env.close();

        AMM amm(env, poolCreator, XRP(10'000), usd(10'000));

        // Alice: 371 XRP, 1 owner (USD trustline).
        // reserve(1) = 250, reserve(2) = 300.
        // Buggy liquid (ownerCountAdj=0): 371 - 250 = 121 XRP
        // Correct liquid (ownerCountAdj=1): 371 - 300 = 71 XRP
        // 1% LP deposit = ~100 XRP. Bug: 121 >= 100 => tesSUCCESS, Fix: 71 < 100 =>
        // tecINSUF_RESERVE_LINE.
        auto const aliceStartXRP = XRP(371) + env.current()->fees().base;
        env.fund(aliceStartXRP, alice);
        env.close();
        env(trust(alice, usd(100'000)));
        env.close();
        env(pay(gw, alice, usd(10'000)));
        env.close();

        {
            auto const aliceSle = env.le(keylet::account(alice.id()));
            BEAST_EXPECT(aliceSle);
            BEAST_EXPECT(aliceSle->getFieldU32(sfOwnerCount) == 1);
            auto const balance = aliceSle->getFieldAmount(sfBalance);
            BEAST_EXPECT(
                balance.xrp() >= XRPAmount{370'000'000} && balance.xrp() <= XRPAmount{372'000'000});
        }

        auto const lptIssue = amm.lptIssue();
        BEAST_EXPECT(
            !env.current()->read(keylet::line(alice.id(), lptIssue.account, lptIssue.currency)));

        // With the FIX: this should fail with tecUNFUNDED_AMM
        // (doApply checkBalance correctly accounts for LP trustline reserve)
        // With the BUG: this would succeed with tesSUCCESS and leave Alice under-reserve
        amm.deposit(
            DepositArg{
                .account = alice,
                .tokens = LPToken(100'000),
                .err = Ter(tecUNFUNDED_AMM)  // Expecting failure with the fix
            });

        // Verify Alice still has ownerCount=1 (deposit failed, no LP trustline created)
        {
            auto const aliceSle = env.le(keylet::account(alice.id()));
            BEAST_EXPECT(aliceSle);
            auto const ownerCount = aliceSle->getFieldU32(sfOwnerCount);
            BEAST_EXPECT(ownerCount == 1);

            // Verify Alice is NOT under-reserve (bug is fixed)
            auto const balance = aliceSle->getFieldAmount(sfBalance);
            auto const requiredReserve = reserve(env, ownerCount);
            BEAST_EXPECT(balance.xrp() >= requiredReserve);
        }

        // Verify LP trustline was NOT created
        BEAST_EXPECT(
            !env.current()->read(keylet::line(alice.id(), lptIssue.account, lptIssue.currency)));

        // Verify Alice has no LP tokens
        auto const aliceLPTokens = amm.getLPTokensBalance(alice.id());
        BEAST_EXPECT(aliceLPTokens == beast::kZero);
    }

    void
    test1814ExistingLPCorrectlyChecked()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1814
        // Control test: Existing LP deposit correctly rejected when balance insufficient
        // (no trustline creation, so ownerCountAdj=0 is correct)

        testcase("Existing LP deposit correctly rejected when balance insufficient (1814 control)");

        using namespace test::jtx;

        auto const features =
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
        Env env{*this, features};

        Account const gw("gw");
        Account const poolCreator("poolCreator");
        Account const bob("bob");

        auto const usd = gw["USD"];

        env.fund(XRP(100'000), gw, poolCreator);
        env.close();
        env(trust(poolCreator, usd(100'000)));
        env.close();
        env(pay(gw, poolCreator, usd(50'000)));
        env.close();

        AMM amm(env, poolCreator, XRP(10'000), usd(10'000));

        env.fund(XRP(1'000), bob);
        env.close();
        env(trust(bob, usd(100'000)));
        env.close();
        env(pay(gw, bob, usd(10'000)));
        env.close();

        // First deposit succeeds, creating LP trustline
        amm.deposit(DepositArg{.account = bob, .tokens = LPToken(10'000), .err = Ter(tesSUCCESS)});

        {
            auto const bobSle = env.le(keylet::account(bob.id()));
            BEAST_EXPECT(bobSle);
            BEAST_EXPECT(bobSle->getFieldU32(sfOwnerCount) == 2);
        }

        // Second deposit with oversized amount should be rejected
        // (ownerCountAdj=0 is correct here since trustline already exists)
        amm.deposit(
            DepositArg{.account = bob, .tokens = LPToken(5'000'000), .err = Ter(tecUNFUNDED_AMM)});
    }

    void
    test1814SingleAssetCaughtByPreclaim()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/1814
        // Control test: Single-asset XRP deposit caught by preclaim
        // (not affected by doApply bug, shows bug only affects tfLPToken path)

        testcase("Single-asset XRP deposit caught by preclaim (1814 control)");

        using namespace test::jtx;

        auto const features =
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
        Env env{*this, features};

        Account const gw("gw");
        Account const poolCreator("poolCreator");
        Account const charlie("charlie");

        auto const usd = gw["USD"];

        env.fund(XRP(100'000), gw, poolCreator);
        env.close();
        env(trust(poolCreator, usd(100'000)));
        env.close();
        env(pay(gw, poolCreator, usd(50'000)));
        env.close();

        AMM amm(env, poolCreator, XRP(10'000), usd(10'000));

        auto const charlieStartXRP = XRP(371) + env.current()->fees().base;
        env.fund(charlieStartXRP, charlie);
        env.close();
        env(trust(charlie, usd(100'000)));
        env.close();
        env(pay(gw, charlie, usd(10'000)));
        env.close();

        // Single-asset deposit should be caught by preclaim
        amm.deposit(
            charlie,
            XRP(100),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            Ter(tecINSUF_RESERVE_LINE));

        auto const lptIssue = amm.lptIssue();
        BEAST_EXPECT(
            !env.current()->read(keylet::line(charlie.id(), lptIssue.account, lptIssue.currency)));
    }

    void
    test2022UnsignedUnderflowAccountReserveOfferCrossing()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2022
        // Bug: Unsigned underflow in accountReserve bypasses owner reserve during
        // sponsored-offer crossing.
        // When ownerCountAdj = -1 and sponsoredOwnerCount >= ownerCount, the
        // subtraction (ownerCount - sponsoredOwnerCount) wraps to SIZE_MAX, which
        // narrows to -1, producing a reserve of (base - increment) instead of
        // (base + increment).

        testcase("test2022 Unsigned underflow in accountReserve during offer crossing");
        using namespace test::jtx;

        auto cfg = makeConfig();
        cfg->fees.referenceFee = 12;
        cfg->fees.accountReserve = 10 * kDropsPerXrp;
        cfg->fees.ownerReserve = 2 * kDropsPerXrp;
        Env env(*this, std::move(cfg));

        auto const issuer = Account("issuer");
        auto const sponsor = Account("sponsor");
        auto const counterparty = Account("counterparty");
        auto const attacker = Account("attacker");

        // Fund accounts
        env.fund(XRP(10'000), issuer, sponsor, counterparty);
        // Attacker gets 10.5 XRP (just above base reserve of 10 XRP)
        env.fund(XRP(10) + drops(500'000), attacker);
        env.close();

        // Create MPTokenIssuance for sponsored object
        MPTTester const mpt(
            {.env = env, .issuer = issuer, .flags = kMptDexFlags, .maxAmt = 1'000'000'000});
        env.close();

        // Sponsor creates SponsorshipSet for attacker (RemainingOwnerCount=1)
        env(sponsor::set_reserve(sponsor, 0, 1), sponsor::SponseeAcc(attacker));
        env.close();

        // Attacker creates a sponsored MPToken => OC=1, SOC=1
        env(mpt.authorizeJV({.account = attacker, .id = mpt.issuanceID()}),
            sponsor::As(sponsor, spfSponsorReserve));
        env.close();

        BEAST_EXPECT(env.ownerCount(attacker) == 1);
        BEAST_EXPECT(env.sponsoredOwnerCount(attacker) == 1);

        // Set up IOU book: issuer enables DefaultRipple, counterparty gets trust line + ATK
        env(fset(issuer, asfDefaultRipple));
        env.close();

        auto const atk = issuer["ATK"];
        env(trust(counterparty, atk(100'000)));
        env.close();

        env(pay(issuer, counterparty, atk(10'000)));
        env.close();

        // Counterparty places offer: 2 ATK for 2 XRP
        env(offer(counterparty, XRP(2), atk(2)));
        env.close();

        // Get attacker's balance before crossing
        auto const attackerBalBefore = env.balance(attacker);

        // Current state: OC=1, SOC=1
        // Required reserve = base + increment * (OC - SOC) = 10 + 2 * (1-1) = 10 XRP
        // Attacker has ~10.5 XRP, so liquid = 10.5 - 10 = 0.5 XRP

        // Attacker crosses the offer by selling XRP for ATK (no trust line for ATK)
        // Offer: Give 2 XRP, Receive 2 ATK (using tfImmediateOrCancel flag)
        //
        // How offer crossing works:
        // 1. XRPEndpointOfferCrossingStep computes reserveReduction = -1
        //    (because ATK trustline doesn't exist yet - see line 206 of XRPEndpointStep.cpp)
        // 2. xrpLiquid() is called with ownerCountAdj = -1
        // 3. In ownerCountHlp: deltaCount = ownerCountAdj - sponsored + sponsoring
        //                                  = -1 - 1 + 0 = -2
        //    confineOwnerCount(1, -2) clamps to 0
        //
        // With the BUG (unsigned underflow in old code):
        //   accountReserve(ownerCount=0, sponsoredOwnerCount=1, ...)
        //   => (0 - 1) wraps to SIZE_MAX in unsigned arithmetic
        //   => narrows to -1 as int64_t
        //   => reserve = base - increment = 10 - 2 = 8 XRP
        //   => liquid = 10.5 - 8 = 2.5 XRP (INFLATED by 2 XRP!)
        //   => Offer would cross the full 2 XRP
        //
        // With the FIX (signed int64_t arithmetic in AccountRootHelpers.cpp):
        //   The calculation stays in signed space, preventing underflow
        //   => reserve calculated correctly
        //   => liquid = ~0.5 XRP (actual available amount)
        //   => Offer can only cross ~0.5 XRP (PARTIAL FILL)
        //
        // Note: tfImmediateOrCancel allows partial fills - it crosses whatever
        // is available immediately and cancels the rest (doesn't place remainder
        // on order book). It returns tesSUCCESS if any amount crossed, tecKILLED
        // if zero crossed. See OfferCreate.cpp lines 818-828.
        env(offer(attacker, atk(2), XRP(2)), Txflags(tfImmediateOrCancel));
        env.close();

        // Verify the transaction succeeded (bug is FIXED)
        auto const attackerBalAfter = env.balance(attacker);

        // Attacker now has OC=2 (MPToken + ATK trustline), SOC=1 (only MPToken is sponsored)
        BEAST_EXPECT(env.ownerCount(attacker) == 2);
        BEAST_EXPECT(env.sponsoredOwnerCount(attacker) == 1);

        // KEY TEST: Verify the offer only partially crossed
        // With the bug, the underflow would have inflated liquid XRP by 2 XRP,
        // allowing the full 2 XRP to be spent.
        // With the fix, only the actual liquid amount (~0.5 XRP) was spent.
        auto const xrpSpent = attackerBalBefore - attackerBalAfter;

        // The attacker should have spent approximately 0.5 XRP (not 2 XRP)
        // This proves the offer crossed only partially due to correct reserve calculation
        BEAST_EXPECT(xrpSpent < XRP(1));  // Less than 1 XRP spent
        BEAST_EXPECT(xrpSpent > XRP(0));  // But greater than 0 (partial crossing succeeded)

        // With the bug, xrpSpent would be ~2 XRP
        // With the fix, xrpSpent is ~0.5 XRP
        // This difference demonstrates that the reserve calculation is working correctly
    }

    void
    test2065SponsorVaultFeeInvariantDeposit()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2065
        // Bug: VaultInvariant incorrectly adds transaction fee to sfAccount XRP delta
        // even when a sponsor paid the fee, causing tecINVARIANT_FAILED for native-XRP
        // vault deposits with sponsor-paid fees.

        testcase("test2065 Sponsor-paid native XRP vault deposit (prefunded)");
        using namespace test::jtx;

        Env env(*this, testableAmendments());
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        env.fund(XRP(1'000'000), alice, bob, sponsor);
        env.close();

        // Create a native-XRP vault
        Vault const vault{env};
        auto const [createTx, keylet] = vault.create({.owner = alice, .asset = xrpIssue()});
        env(createTx);
        env.close();

        // Control: un-sponsored deposit should succeed
        auto const depositTx1 =
            vault.deposit({.depositor = bob, .id = keylet.key, .amount = XRP(100)});
        env(depositTx1);
        env.close();
        BEAST_EXPECT(env.le(keylet)->at(sfAssetsTotal) == XRP(100).value());

        // Setup prefunded sponsor fee
        auto const feeAmt = drops(env.current()->fees().base.drops());
        env(sponsor::set_fee(sponsor, 0, feeAmt + XRP(1)), sponsor::SponseeAcc(bob));
        env.close();

        // Test: prefunded sponsor-fee deposit
        // With the bug, VaultInvariant adds the sponsor-paid fee to bob's XRP delta,
        // causing the invariant check to fail with tecINVARIANT_FAILED
        // With the fix, the invariant correctly skips fee compensation when sponsor paid
        auto const depositTx2 =
            vault.deposit({.depositor = bob, .id = keylet.key, .amount = XRP(100)});
        env(depositTx2, Fee(feeAmt), sponsor::As(sponsor, spfSponsorFee));
        env.close();

        BEAST_EXPECT(env.le(keylet)->at(sfAssetsTotal) == XRP(200).value());
    }

    void
    test2065SponsorVaultFeeInvariantWithdraw()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2065
        // Bug: VaultInvariant incorrectly adds transaction fee to sfAccount XRP delta
        // even when a sponsor paid the fee, causing tecINVARIANT_FAILED for native-XRP
        // vault self-withdrawals with sponsor-paid fees.

        testcase("test2065 Sponsor-paid native XRP vault self-withdraw (co-signed)");
        using namespace test::jtx;

        Env env(*this, testableAmendments());
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        env.fund(XRP(1'000'000), alice, bob, sponsor);
        env.close();

        // Create a native-XRP vault and deposit
        Vault const vault{env};
        auto const [createTx, keylet] = vault.create({.owner = alice, .asset = xrpIssue()});
        env(createTx);
        env.close();
        env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = XRP(100)}));
        env.close();

        // Control: un-sponsored self-withdrawal should succeed
        auto const withdrawTx1 =
            vault.withdraw({.depositor = bob, .id = keylet.key, .amount = XRP(40)});
        env(withdrawTx1);
        env.close();
        BEAST_EXPECT(env.le(keylet)->at(sfAssetsTotal) == XRP(60).value());

        // Test: co-signed sponsor-fee self-withdrawal
        // With the bug, VaultInvariant adds the sponsor-paid fee to bob's XRP delta,
        // causing the invariant check to fail with tecINVARIANT_FAILED
        // With the fix, the invariant correctly skips fee compensation when sponsor paid
        auto const feeAmt = drops(env.current()->fees().base.drops());
        auto const withdrawTx2 =
            vault.withdraw({.depositor = bob, .id = keylet.key, .amount = XRP(40)});
        env(withdrawTx2,
            Fee(feeAmt),
            sponsor::As(sponsor, spfSponsorFee),
            Sig(sfSponsorSignature, sponsor));
        env.close();

        BEAST_EXPECT(env.le(keylet)->at(sfAssetsTotal) == XRP(20).value());
    }

    void
    test2158SponsorLoanBrokerSetMPTPseudoAccountInvariant()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2158
        // Bug #2158 (FIXED): LoanBrokerSet on an MPT-asset vault with sponsor.
        //
        // Original bug (contest code, April 2026):
        // authorizeMPToken called getTxReserveSponsor(view, tx) WITHOUT the account
        // parameter, causing it to return the sponsor even for pseudo-accounts.
        // This propagated sponsor fields onto the broker pseudo-account's MPToken,
        // triggering the "pseudo-account must not have sponsorship fields" invariant
        // => tecINVARIANT_FAILED.
        //
        // Fix applied (June 7, 2026, commit 8f3e5f93):
        // Changed to getTxReserveSponsor(view, tx, account) WITH the account parameter.
        // Now when account is a pseudo-account, it correctly returns std::nullopt,
        // preventing sponsorship fields from being added to pseudo-accounts.
        //
        // This test verifies the fix works correctly.

        testcase("test2158 LoanBrokerSet + sponsor + MPT vault (bug fixed)");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const alice{"alice2"};
        Account const bob{"bob2"};

        env.fund(XRP(100'000), alice, bob);
        env.close();

        MPTTester mpt{env, alice, kMptInitNoFund};
        mpt.create({.flags = tfMPTCanTransfer});
        env.close();

        PrettyAsset const asset = mpt["MPT"];

        Vault const vault{env};
        auto const [createTx, vaultKeylet] = vault.create({.owner = alice, .asset = asset});
        env(createTx);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice));

        // With the fix: transaction succeeds (pseudo-account does not get sponsor fields)
        env(loanBroker::set(alice.id(), vaultKeylet.key),
            sponsor::As(bob, spfSponsorReserve),
            Sig(sfSponsorSignature, bob),
            Ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(env.le(brokerKeylet));

        // Control: same LoanBrokerSet without a sponsor also succeeds.
        auto const brokerKeylet2 = keylet::loanbroker(alice.id(), env.seq(alice));
        env(loanBroker::set(alice.id(), vaultKeylet.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.le(brokerKeylet2));
    }

    void
    test2178TrustSetCounterpartySponsorMisroute()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2178
        // TrustSet's modify path applies the tx-level reserve sponsor to whichever
        // side has its reserve gate trip on this update, regardless of whether that
        // side belongs to the tx submitter. trustCreate only sets the submitter's
        // reserve flag and snapshots the counterparty's asfDefaultRipple state into
        // the line's NoRipple bit; if the counterparty later toggles asfDefaultRipple
        // (the canonical issuer flow), the line and account flags disagree and on
        // the submitter's next TrustSet the counterparty-side gate fires without
        // the canonical tx[sfAccount] == account guard.
        //
        // With the fix: getTxReserveSponsor() checks if the account parameter
        // matches tx[sfAccount], preventing sponsor misroute to counterparty.

        testcase("test2178 TrustSet modify with sponsor does not misroute onto counterparty side");

        using namespace test::jtx;

        Env env(*this);
        Account const alice{"alice_t2178"};
        Account const bob{"bob_t2178"};
        Account const carol{"carol_t2178"};

        // Fund without auto-setting asfDefaultRipple
        env.fund(XRP(100'000), alice);
        env.fund(XRP(100'000), bob);
        env.fund(XRP(100'000), carol);
        env.close();

        auto const usd = bob["USD"];

        // Alice creates the trust line. trustCreate sets only Alice's reserve flag
        // and (because Bob has no DefaultRipple yet) sets lsfHighNoRipple on Bob's side.
        // No sponsor attached.
        env(trust(alice, usd(1'000)));
        env.close();

        // Bob now enables asfDefaultRipple (canonical issuer-side flow).
        // The line still carries lsfHighNoRipple on Bob's side, so on Alice's next
        // TrustSet the gate ((uFlagsOut & lsfHighNoRipple) == 0) != bHighDefRipple
        // evaluates to (false != true) = true, and bHighReserved is still false.
        env(fset(bob, asfDefaultRipple));
        env.close();

        bool const aliceIsHigh = alice.id() > bob.id();
        SF_ACCOUNT const& bobSponsorField = aliceIsHigh ? sfLowSponsor : sfHighSponsor;
        SF_ACCOUNT const& aliceSponsorField = aliceIsHigh ? sfHighSponsor : sfLowSponsor;

        auto const lineKey = keylet::line(alice, bob, usd.currency);
        auto const sleLineBefore = env.le(lineKey);
        BEAST_EXPECT(sleLineBefore);
        BEAST_EXPECT(!sleLineBefore->isFieldPresent(sfLowSponsor));
        BEAST_EXPECT(!sleLineBefore->isFieldPresent(sfHighSponsor));

        auto const carolBefore = (*env.le(carol))[~sfSponsoringOwnerCount].value_or(0u);
        auto const bobBefore = (*env.le(bob))[~sfSponsoredOwnerCount].value_or(0u);

        // Alice modifies the trust line with Carol as sponsor
        env(trust(alice, usd(2'000)),
            sponsor::As(carol, spfSponsorReserve),
            Sig(sfSponsorSignature, carol),
            Ter(tesSUCCESS));
        env.close();

        auto const sleLineAfter = env.le(lineKey);
        BEAST_EXPECT(sleLineAfter);

        // With the fix: Bob's side should NOT have Carol as sponsor
        // Carol only agreed to back Alice
        BEAST_EXPECT(!sleLineAfter->isFieldPresent(bobSponsorField));

        // Alice's side also has no sponsor because Alice's reserve flag was
        // already set on the FIRST TrustSet (no sponsor in scope then), so
        // bAliceReserveSet && !bAliceReserved is false on the modify path
        BEAST_EXPECT(!sleLineAfter->isFieldPresent(aliceSponsorField));

        // Carol's sponsoring count should remain unchanged (no misroute)
        auto const carolAfter = (*env.le(carol))[~sfSponsoringOwnerCount].value_or(0u);
        BEAST_EXPECT(carolAfter == carolBefore);

        // Bob's sponsored count should remain unchanged
        auto const bobAfter = (*env.le(bob))[~sfSponsoredOwnerCount].value_or(0u);
        BEAST_EXPECT(bobAfter == bobBefore);
    }

    void
    test2241AlternateFeePayerQueueAdmissionDelegate()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2241
        testcase("TxQ alternate fee payer queue admission - delegated");
        using namespace jtx;

        auto cfg = makeConfig({{"minimum_txn_in_ledger_standalone", "3"}});
        cfg->fees.referenceFee = 10;
        cfg->fees.accountReserve = 200;
        cfg->fees.ownerReserve = 50;  // * kDropsPerXrp;
        Env env{*this, std::move(cfg)};

        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(500'000), alice, bob);
        env.close();

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        Account const burner{"burner"};
        env.fund(XRP(1'000'000), burner);
        env.close();
        fillQueue(env, burner);
        fillQueue(env, burner);

        auto const aliceBalanceBefore = env.balance(alice);
        auto const bobBalanceBefore = env.balance(bob);
        auto const aliceSeq = env.seq(alice);

        for (int i = 0; i < 4; ++i)
        {
            env(pay(alice, bob, drops(1)),
                delegate::As(bob),
                Seq(aliceSeq + i),
                Fee(51),
                Ter(terQUEUED));
        }

        auto const aliceBalance1 = env.balance(alice);
        auto const bobBalance1 = env.balance(bob);

        // Fixed: the fifth tx should be queued because Bob is the real fee
        // payer and has enough XRP. Before the fix, TxQ summed the four prior fees
        // against Alice's reserve: 4 * 51 drops >= 200 drops.
        env(pay(alice, bob, drops(1)),
            delegate::As(bob),
            Seq(aliceSeq + 4),
            Fee(51),
            Ter(terQUEUED));  // fixed, originally telCAN_NOT_QUEUE_BALANCE

        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == 5);

        // Drain the queue
        for (int i = 0; i < 4 && env.app().getTxQ().getMetrics(*env.current()).txCount != 0; ++i)
            env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 5);
        BEAST_EXPECT(
            env.balance(alice) == aliceBalanceBefore - drops(5));  // Alice sends 5 x 1 drop
        BEAST_EXPECT(
            env.balance(bob) ==
            bobBalanceBefore + drops(5) - drops(51 * 5));  // Bob receives 5 drops, pays 5 fees
    }

    void
    test2241AlternateFeePayerQueueAdmissionSponsored()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2241
        testcase("TxQ alternate fee payer queue admission - sponsored");
        using namespace jtx;

        auto cfg = makeConfig({{"minimum_txn_in_ledger_standalone", "3"}});
        cfg->fees.referenceFee = 10;
        cfg->fees.accountReserve = 200;
        cfg->fees.ownerReserve = 50;
        Env env{*this, std::move(cfg)};

        Account const alice{"alice"};
        Account const sponsor{"sponsor"};

        env.fund(XRP(500000), alice, sponsor);
        env.close();

        Account const burner{"burner"};
        env.fund(XRP(1'000'000), burner);
        env.close();
        fillQueue(env, burner);
        fillQueue(env, burner);

        auto const aliceBalanceBefore = env.balance(alice);
        auto const sponsorBalanceBefore = env.balance(sponsor);
        auto const aliceSeq = env.seq(alice);

        for (int i = 0; i < 4; ++i)
        {
            env(noop(alice),
                sponsor::As(sponsor, spfSponsorFee),
                Sig(sfSponsorSignature, sponsor),
                Seq(aliceSeq + i),
                Fee(51),
                Ter(terQUEUED));
        }

        auto const aliceBalance1 = env.balance(alice);
        auto const sponsorBalance1 = env.balance(sponsor);
        // Fixed: co-signed fee sponsorship pays from sponsor, not Alice.
        env(noop(alice),
            sponsor::As(sponsor, spfSponsorFee),
            Sig(sfSponsorSignature, sponsor),
            Seq(aliceSeq + 4),
            Fee(51),
            Ter(terQUEUED));  // fixed, originally telCAN_NOT_QUEUE_BALANCE

        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == 5);

        // Drain the queue
        for (int i = 0; i < 4 && env.app().getTxQ().getMetrics(*env.current()).txCount != 0; ++i)
            env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 5);
        BEAST_EXPECT(env.balance(alice) == aliceBalanceBefore);
        BEAST_EXPECT(env.balance(sponsor) == sponsorBalanceBefore - drops(51 * 5));
    }

    void
    test2284LegacySignerListReserveMismatch()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2284
        // Legacy SignerList sponsorship under-charges create at 1 ReserveCount but refunds 2 +
        // signer_count on delete, inflating pre-funded quota
        testcase("legacy SignerList sponsorship reserve mismatch");
        using namespace jtx;

        Env env{*this, testableAmendments()};

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        Account const sponsor("sponsor");

        std::uint32_t constexpr legacyOwnerCount = 5;  // 2 + 3 signers

        env.fund(XRP(1000000), alice, bob, carol, dave, sponsor);
        env.close();

        // Create a modern SignerList with 3 signers
        env(jtx::signers(alice, 1, {{bob, 1}, {carol, 1}, {dave, 1}}), Ter(tesSUCCESS));
        env.close();

        auto const signerListKeylet = keylet::signers(alice.id());
        auto const sponsorKeylet = keylet::sponsorship(sponsor.id(), alice.id());

        // Verify modern SignerList has lsfOneOwnerCount
        {
            auto const signerList = env.le(signerListKeylet);
            BEAST_EXPECT(signerList);
            if (signerList)
                BEAST_EXPECT((signerList->getFlags() & lsfOneOwnerCount) != 0);
        }
        BEAST_EXPECT(env.ownerCount(alice) == 1);

        // Synthesize legacy SignerList by clearing lsfOneOwnerCount
        // and restoring legacy OwnerCount (2 + signer_count = 5)
        // This simulates historical state from pre-MultiSignReserve amendment
        auto const changed = env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            Sandbox sb(&view, TapNone);

            auto const legacySignerList = sb.peek(signerListKeylet);
            auto const legacyAccount = sb.peek(keylet::account(alice.id()));
            if (!legacySignerList || !legacyAccount)
                return false;

            legacySignerList->clearFlag(lsfOneOwnerCount);
            legacyAccount->setFieldU32(sfOwnerCount, legacyOwnerCount);

            sb.update(legacySignerList);
            sb.update(legacyAccount);
            sb.apply(view);
            return true;
        });

        BEAST_EXPECT(changed);

        // Verify legacy state (do not close ledger after open ledger modification)
        {
            auto const signerList = env.le(signerListKeylet);
            BEAST_EXPECT(signerList);
            if (signerList)
                BEAST_EXPECT((signerList->getFlags() & lsfOneOwnerCount) == 0);
        }
        BEAST_EXPECT(env.ownerCount(alice) == legacyOwnerCount);
        BEAST_EXPECT(env.sponsoredOwnerCount(alice) == 0);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == 0);

        // Create pre-funded Sponsorship with enough reserves for legacy SignerList
        env(sponsor::set_reserve(sponsor, 0, legacyOwnerCount),
            sponsor::SponseeAcc(alice),
            Ter(tesSUCCESS));

        {
            auto const sponsorSle = env.le(sponsorKeylet);
            BEAST_EXPECT(sponsorSle);
            if (sponsorSle)
                BEAST_EXPECT(sponsorSle->getFieldU32(sfRemainingOwnerCount) == legacyOwnerCount);
        }

        // FIX VERIFIED: SponsorshipTransfer Create now correctly consumes 5 reserve units
        env(sponsor::transfer(alice, tfSponsorshipCreate, signerListKeylet.key),
            sponsor::As(sponsor, spfSponsorReserve),
            Ter(tesSUCCESS));

        {
            auto const signerList = env.le(signerListKeylet);
            BEAST_EXPECT(signerList);
            if (signerList)
                BEAST_EXPECT(signerList->getAccountID(sfSponsor) == sponsor.id());
        }
        BEAST_EXPECT(env.ownerCount(alice) == legacyOwnerCount);
        BEAST_EXPECT(env.sponsoredOwnerCount(alice) == legacyOwnerCount);     // FIX: correctly 5
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == legacyOwnerCount);  // FIX: correctly 5

        // Verify sfRemainingOwnerCount was correctly drained from 5 to absent
        {
            auto const sponsorSle = env.le(sponsorKeylet);
            BEAST_EXPECT(sponsorSle);
            if (sponsorSle)
                BEAST_EXPECT(!sponsorSle->isFieldPresent(sfRemainingOwnerCount));
        }

        // Delete SignerList - should refund 5 units (2 + 3 signers)
        env(jtx::signers(alice, jtx::kNone), Ter(tesSUCCESS));

        BEAST_EXPECT(!env.le(signerListKeylet));
        BEAST_EXPECT(env.ownerCount(alice) == 0);
        BEAST_EXPECT(env.sponsoredOwnerCount(alice) == 0);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == 0);

        // FIX VERIFIED: With symmetric accounting, create and delete both use weight 5
        // Note: sfRemainingOwnerCount is ONLY consumed on create, never refunded on delete
        // (by design per AccountRootHelpers.cpp:423-431). The refund only affects
        // sfSponsoringOwnerCount/sfSponsoredOwnerCount which correctly went from 5 to 0.
        // The key fix verification is that NO INFLATION occurred: the pre-funded quota
        // was fully consumed (5 units), and the sponsoring/sponsored counts are symmetric.
        {
            auto const sponsorSle = env.le(sponsorKeylet);
            BEAST_EXPECT(sponsorSle);
            if (sponsorSle)
            {
                // sfRemainingOwnerCount should remain absent after delete (not refunded)
                // This is correct behavior - the pre-funded quota was consumed
                BEAST_EXPECT(!sponsorSle->isFieldPresent(sfRemainingOwnerCount));
            }
        }
    }

    void
    test2320LoanSetBorrowerSponsorNotAppliedToLenderTrustline()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2320
        // LoanSet can apply a borrower reserve sponsor to lender-owned holdings and lock usable
        // sponsor XRP
        testcase("LoanSet borrower sponsor not misapplied to lender trustline");
        using namespace jtx;
        using namespace loanBroker;
        using namespace loan;

        Account const borrower("borrower");
        Account const lender("lender");
        Account const issuer("issuer");
        Account const sponsor("sponsor");
        auto const usd = issuer["USD"];
        PrettyAsset const asset{usd.issue()};

        Env env{*this, testableAmendments()};
        env.fund(XRP(1000000), borrower, lender, issuer, sponsor);
        env.close();

        env(trust(borrower, usd(100000)));
        env(trust(lender, usd(100000)));
        env.close();

        env(pay(issuer, borrower, usd(1000)));
        env(pay(issuer, lender, usd(1000)));
        env.close();

        Vault const vault{env};
        auto const [vaultCreate, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(vaultCreate);
        env.close();

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = usd(100)}));
        env.close();

        auto const brokerKeylet = keylet::loanbroker(lender.id(), env.seq(lender));
        env(set(lender, vaultKeylet.key, 0));
        env.close();
        env(coverDeposit(lender, brokerKeylet.key, usd(100)));
        env.close();

        auto const lenderLine = keylet::line(lender, issuer, usd.issue().currency);
        auto const lenderLineBefore = env.le(lenderLine);
        BEAST_EXPECT(lenderLineBefore != nullptr);
        if (!lenderLineBefore)
            return;

        // Lender removes their trust line
        env(trust(lender, usd(0)));
        env.close();
        env(pay(lender, issuer, asset(abs(lenderLineBefore->at(sfBalance).value()))));
        env.close();

        BEAST_EXPECT(env.le(lenderLine) == nullptr);

        // Leave lender just below reserve for one more object
        adjustAccountXRPBalance(env, lender, reserve(env, env.ownerCount(lender)) + drops(1000));
        BEAST_EXPECT(env.balance(lender) < reserve(env, env.ownerCount(lender) + 1));

        auto const loanSeq = env.le(brokerKeylet)->getFieldU32(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSeq);

        // FIX VERIFIED: Borrower-sponsored LoanSet with origination fee
        // should create lender trust line WITHOUT the borrower's sponsor
        env(set(borrower, brokerKeylet.key, asset(10).value()),
            kLoanOriginationFee(asset(1).value()),
            kCounterparty(lender),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 5),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tecNO_LINE_INSUF_RESERVE));  // FIX: lender must self-fund, tesSUCCESS BEFORE FIX
        env.close();

        // Verify loan was NOT created since lender can't self-fund the line
        BEAST_EXPECT(env.le(loanKeylet) == nullptr);

        // Verify borrower sponsor was NOT charged for lender's line
        BEAST_EXPECT(env.sponsoredOwnerCount(borrower) == 0);
        BEAST_EXPECT(env.sponsoredOwnerCount(lender) == 0);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == 0);

        // Verify lender trust line was NOT recreated
        BEAST_EXPECT(env.le(lenderLine) == nullptr);
    }

    void
    test2320LoanSetBorrowerSponsorNotAppliedToLenderMptHolding()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2320
        // LoanSet can apply a borrower reserve sponsor to lender-owned holdings and lock usable
        // sponsor XRP
        testcase("LoanSet borrower sponsor not misapplied to lender MPT holding");
        using namespace jtx;
        using namespace loanBroker;
        using namespace loan;

        Account const borrower("borrower");
        Account const lender("lender");
        Account const issuer("issuer");
        Account const sponsor("sponsor");

        Env env{*this, testableAmendments()};
        env.fund(XRP(1000000), borrower, lender, issuer, sponsor);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        env.close();
        PrettyAsset const asset = mptt["MPT"];
        mptt.authorize({.account = borrower});
        mptt.authorize({.account = lender});
        env.close();

        env(pay(issuer, borrower, asset(1000)));
        env(pay(issuer, lender, asset(1000)));
        env.close();

        Vault const vault{env};
        auto const [vaultCreate, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(vaultCreate);
        env.close();
        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = asset(100)}));
        env.close();

        auto const brokerKeylet = keylet::loanbroker(lender.id(), env.seq(lender));
        env(set(lender, vaultKeylet.key, 0));
        env.close();
        env(coverDeposit(lender, brokerKeylet.key, asset(100)));
        env.close();

        auto const lenderMpt = keylet::mptoken(mptt.issuanceID(), lender);
        auto const lenderMptBefore = env.le(lenderMpt);
        BEAST_EXPECT(lenderMptBefore != nullptr);
        if (!lenderMptBefore)
            return;

        // Lender removes their MPT holding
        env(pay(lender, issuer, asset(lenderMptBefore->at(sfMPTAmount))));
        env.close();
        mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
        env.close();

        BEAST_EXPECT(env.le(lenderMpt) == nullptr);

        // Leave lender just below reserve for one more object
        adjustAccountXRPBalance(env, lender, reserve(env, env.ownerCount(lender)) + drops(1000));
        BEAST_EXPECT(env.balance(lender) < reserve(env, env.ownerCount(lender) + 1));

        auto const loanSeq = env.le(brokerKeylet)->getFieldU32(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSeq);

        // FIX VERIFIED: Borrower-sponsored LoanSet with origination fee
        // should NOT apply sponsor to lender's MPT holding
        env(set(borrower, brokerKeylet.key, asset(10).value()),
            kLoanOriginationFee(asset(1).value()),
            kCounterparty(lender),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 5),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tecINSUFFICIENT_RESERVE));  // FIX: lender must self-fund
        env.close();

        // Verify loan was NOT created
        BEAST_EXPECT(env.le(loanKeylet) == nullptr);

        // Verify borrower sponsor was NOT charged for lender's holding
        BEAST_EXPECT(env.sponsoredOwnerCount(borrower) == 0);
        BEAST_EXPECT(env.sponsoredOwnerCount(lender) == 0);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == 0);

        // Verify lender MPT holding was NOT recreated
        BEAST_EXPECT(env.le(lenderMpt) == nullptr);
    }

    void
    test2320LoanSetBorrowerSponsorOnlySponsorsIntendedBorrowerLoan()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2320
        // LoanSet can apply a borrower reserve sponsor to lender-owned holdings and lock usable
        // sponsor XRP
        testcase("LoanSet borrower sponsor only applies to intended borrower loan");
        using namespace jtx;
        using namespace loanBroker;
        using namespace loan;

        Account const borrower("borrower");
        Account const lender("lender");
        Account const issuer("issuer");
        Account const sponsor("sponsor");
        auto const usd = issuer["USD"];
        PrettyAsset const asset{usd.issue()};

        Env env{*this, testableAmendments()};
        env.fund(XRP(1000000), borrower, lender, issuer, sponsor);
        env.close();

        env(trust(borrower, usd(100000)));
        env(trust(lender, usd(100000)));
        env.close();

        env(pay(issuer, borrower, usd(1000)));
        env(pay(issuer, lender, usd(1000)));
        env.close();

        Vault const vault{env};
        auto const [vaultCreate, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(vaultCreate);
        env.close();

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = usd(100)}));
        env.close();

        auto const brokerKeylet = keylet::loanbroker(lender.id(), env.seq(lender));
        env(set(lender, vaultKeylet.key, 0));
        env.close();
        env(coverDeposit(lender, brokerKeylet.key, usd(100)));
        env.close();

        auto const loanSeq = env.le(brokerKeylet)->getFieldU32(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSeq);

        // FIX VERIFIED: With lender having sufficient reserve and existing trust line,
        // sponsor correctly applies ONLY to borrower-owned loan object
        env(set(borrower, brokerKeylet.key, asset(10).value()),
            kLoanOriginationFee(asset(1).value()),
            kCounterparty(lender),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 5),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tesSUCCESS));
        env.close();

        auto const loan = env.le(loanKeylet);
        BEAST_EXPECT(loan != nullptr);
        if (!loan)
            return;

        // Verify loan is sponsored by borrower's sponsor
        BEAST_EXPECT(loan->isFieldPresent(sfSponsor));
        BEAST_EXPECT(loan->getAccountID(sfSponsor) == sponsor.id());

        // Verify lender trust line is NOT sponsored
        auto const lenderLine = keylet::line(lender, issuer, usd.issue().currency);
        auto const lenderLineAfter = env.le(lenderLine);
        BEAST_EXPECT(lenderLineAfter != nullptr);
        if (lenderLineAfter)
        {
            auto const& lenderSponsorField =
                lender.id() > issuer.id() ? sfHighSponsor : sfLowSponsor;
            // FIX: lender side is NOT sponsored by borrower's sponsor
            BEAST_EXPECT(!lenderLineAfter->isFieldPresent(lenderSponsorField));
        }

        // Verify counts: only borrower loan is sponsored
        BEAST_EXPECT(env.sponsoredOwnerCount(borrower) == 1);
        BEAST_EXPECT(env.sponsoredOwnerCount(lender) == 0);  // FIX: lender NOT sponsored
        BEAST_EXPECT(
            env.sponsoringOwnerCount(sponsor) == 1);  // FIX: sponsor only sponsors 1 object
    }

    void
    test2320BorrowerSponsorReserveLockScalesAcrossManyLenders()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2320
        // LoanSet can apply a borrower reserve sponsor to lender-owned holdings and lock usable
        // sponsor XRP
        testcase("LoanSet borrower sponsor reserve lock scales across lender trustlines");
        using namespace jtx;
        using namespace loanBroker;
        using namespace loan;

        Account const borrower("borrower");
        Account const issuer("issuer");
        Account const sponsor("sponsor");
        auto const usd = issuer["USD"];
        PrettyAsset const asset{usd.issue()};

        Env env{*this, testableAmendments()};
        env.fund(XRP(1000000), borrower, issuer, sponsor);

        // Each colluding lender can force one unintended lender-owned trust line
        // reserve onto the same sponsor. This is a griefing / reserve-lock scale
        // test, not a profit-extraction test.
        std::uint32_t constexpr forcedLoans = 100;
        std::vector<Account> lenders;
        lenders.reserve(forcedLoans);
        for (std::uint32_t i = 0; i < forcedLoans; ++i)
        {
            lenders.emplace_back("lender" + std::to_string(i));
            env.fund(XRP(1000000), lenders.back());
        }
        env.close();

        env(trust(borrower, usd(1000000)));
        for (auto const& lender : lenders)
            env(trust(lender, usd(1000000)));
        env.close();

        env(pay(issuer, borrower, usd(1000000)));
        for (auto const& lender : lenders)
            env(pay(issuer, lender, usd(1000)));
        env.close();

        std::vector<Keylet> brokerKeylets;
        brokerKeylets.reserve(forcedLoans);
        Vault const vault{env};
        for (auto const& lender : lenders)
        {
            auto const [vaultCreate, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
            env(vaultCreate);
            env.close();

            env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = usd(100)}));
            env.close();

            auto const brokerKeylet = keylet::loanbroker(lender.id(), env.seq(lender));
            env(set(lender, vaultKeylet.key, 0));
            env.close();
            env(coverDeposit(lender, brokerKeylet.key, usd(100)));
            env.close();

            auto const lenderLine = keylet::line(lender, issuer, usd.issue().currency);
            auto const lenderLineBefore = env.le(lenderLine);
            BEAST_EXPECT(lenderLineBefore != nullptr);
            if (!lenderLineBefore)
                return;

            // Remove the lender's holding so LoanSet must recreate it when paying
            // the origination fee to the lender / LoanBroker owner.
            env(trust(lender, usd(0)));
            env.close();
            env(pay(lender, issuer, asset(abs(lenderLineBefore->at(sfBalance).value()))));
            env.close();

            BEAST_EXPECT(env.le(lenderLine) == nullptr);

            // Give lender enough reserve to self-fund the recreated trust line
            // (verifying that the fix ensures lender must self-fund, not borrower sponsor)
            auto const lenderNextReserve = reserve(env, env.ownerCount(lender) + 1);
            adjustAccountXRPBalance(env, lender, lenderNextReserve + drops(10));
            BEAST_EXPECT(env.balance(lender) >= lenderNextReserve);

            brokerKeylets.push_back(brokerKeylet);
        }

        // FIX VERIFIED: All LoanSet transactions succeed with lender self-funding
        for (std::uint32_t i = 0; i < forcedLoans; ++i)
        {
            auto const& lender = lenders[i];
            auto const& brokerKeylet = brokerKeylets[i];
            auto const loanSeq = env.le(brokerKeylet)->getFieldU32(sfLoanSequence);
            auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSeq);

            // FIX: Each transaction succeeds - borrower's loan is sponsored,
            // but lender's trust line is NOT sponsored (lender self-funds)
            env(set(borrower, brokerKeylet.key, asset(10).value()),
                kLoanOriginationFee(asset(1).value()),
                kCounterparty(lender),
                Sig(sfCounterpartySignature, lender),
                Fee(env.current()->fees().base * 5),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor),
                Ter(tesSUCCESS));
            env.close();

            // FIX: Verify loan WAS created and IS sponsored by sponsor
            auto const loanLE = env.le(loanKeylet);
            BEAST_EXPECT(loanLE != nullptr);
            if (loanLE)
                BEAST_EXPECT(loanLE->at(sfSponsor) == sponsor.id());

            // FIX: Verify lender trust line WAS recreated but is NOT sponsored
            auto const lenderLine = keylet::line(lender, issuer, usd.issue().currency);
            auto const lenderLineLE = env.le(lenderLine);
            BEAST_EXPECT(lenderLineLE != nullptr);
            if (lenderLineLE)
                BEAST_EXPECT(!lenderLineLE->isFieldPresent(sfSponsor));
        }

        // FIX VERIFIED: Sponsor only sponsors borrower loans (100), not lender holdings
        BEAST_EXPECT(env.sponsoredOwnerCount(borrower) == forcedLoans);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == forcedLoans);

        // FIX: All lenders have 0 sponsored objects (self-funded their trust lines)
        for (auto const& lender : lenders)
            BEAST_EXPECT(env.sponsoredOwnerCount(lender) == 0);

        // FIX: Sponsor can still use their balance (not locked by unintended sponsorships)
        auto const sponsorBalance = env.balance(sponsor);
        auto const payAmount = XRP(100);
        BEAST_EXPECT(sponsorBalance > payAmount);
        env(pay(sponsor, borrower, payAmount), Ter(tesSUCCESS));
        env.close();

        // NOTE: Sponsor cannot delete account because they are sponsoring 100 borrower loans
        // This is EXPECTED behavior - sponsor has legitimate obligations from the loans they
        // sponsored
    }

    void
    test2342EscrowFinishIouSameSponsorRecycleReserve()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2342

        // BUG: EscrowFinish can falsely reject a same-sponsor finish when the sponsor
        // funds both the escrow being deleted and the destination trust line being created.
        // The net reserve change is zero (delete escrow -1, create trustline +1), but the
        // implementation checks reserve BEFORE freeing the escrow, causing false rejection.
        //
        // IMPACT: For no-cancel escrows, funds become locked - sender can't cancel,
        // receiver doesn't receive, and same-sponsor path is falsely blocked.

        // EscrowFinish same-sponsor reserve recycling for IOU escrows
        testcase("EscrowFinish IOU same-sponsor recycle reserve");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const gw{"gw"};
        Account const sponsor{"sponsor"};
        IOU const usd = gw["USD"];

        Env env{*this, testableAmendments()};
        env.fund(XRP(10000), alice, bob, gw, sponsor);
        env.close();
        auto const baseFee = env.current()->fees().base;

        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        auto const initialUSD = usd(1'000);
        auto const lockedUSD = usd(900);

        env(trust(alice, usd(2'000)));
        env.close();
        env(pay(gw, alice, initialUSD));
        env.close();

        env(ticket::create(bob, 2));
        env.close();
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        auto const preAliceUSD = env.balance(alice, usd);
        BEAST_EXPECT(preAliceUSD == initialUSD);

        std::uint32_t const seq = env.seq(alice);
        env(escrow::create(alice, bob, lockedUSD),
            escrow::kFinishTime(env.now() + std::chrono::seconds{1}),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();
        env.close(std::chrono::seconds{2});
        BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD - lockedUSD);

        auto const escrowKeylet = keylet::escrow(alice, seq);
        auto sleEscrow = env.le(escrowKeylet);
        BEAST_EXPECT(sleEscrow != nullptr);
        if (!sleEscrow)
            return;

        BEAST_EXPECT(sleEscrow->getFieldAmount(sfAmount) == lockedUSD);
        BEAST_EXPECT(sleEscrow->isFieldPresent(sfSponsor));
        BEAST_EXPECT(sleEscrow->getAccountID(sfSponsor) == sponsor.id());
        BEAST_EXPECT(!sleEscrow->isFieldPresent(sfCancelAfter));
        BEAST_EXPECT(ownerCount(env, alice) == 2);
        BEAST_EXPECT(ownerCount(env, bob) == 2);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // Set sponsor to exactly afford the currently sponsored Escrow, but
        // not a second sponsored object. Same-sponsor finish should be net-zero
        // because the Escrow is deleted as the destination line is created.
        adjustAccountXRPBalance(env, sponsor, reserve(env, 1));
        adjustAccountXRPBalance(env, bob, reserve(env, 2) + (baseFee * 10));
        BEAST_EXPECT(env.balance(sponsor) == reserve(env, 1));
        BEAST_EXPECT(env.balance(bob) < reserve(env, 3));

        // Without sponsor, fails with tecNO_LINE_INSUF_RESERVE (expected - bob can't self-fund)
        env(escrow::finish(bob, alice, seq), Fee(baseFee), Ter(tecNO_LINE_INSUF_RESERVE));
        env.close();

        // FIX VERIFIED: With same sponsor, succeeds (net-zero reserve change)
        // The sponsor recycles reserve from escrow deletion to trustline creation
        // WITHOUT FIX: fails with tecNO_LINE_INSUF_RESERVE
        env(escrow::finish(bob, alice, seq),
            Fee(baseFee),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tesSUCCESS));
        env.close();

        // FIX: Escrow has been successfully finished
        sleEscrow = env.le(escrowKeylet);
        BEAST_EXPECT(sleEscrow == nullptr);

        // FIX: Bob received the escrowed funds
        BEAST_EXPECT(env.balance(bob, usd) == lockedUSD);
        BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD - lockedUSD);

        // FIX: Bob's trustline was created and is sponsored
        auto const bobLine = env.le(keylet::line(bob, usd.issue()));
        BEAST_EXPECT(bobLine != nullptr);
        if (bobLine)
        {
            auto const& bobSponsorField = bob.id() > gw.id() ? sfHighSponsor : sfLowSponsor;
            BEAST_EXPECT(bobLine->isFieldPresent(bobSponsorField));
            BEAST_EXPECT(bobLine->getAccountID(bobSponsorField) == sponsor.id());
        }

        // FIX: Owner counts updated correctly (escrow deleted, trustline created)
        BEAST_EXPECT(ownerCount(env, alice) == 1);           // only alice's trustline remains
        BEAST_EXPECT(ownerCount(env, bob) == 3);             // 2 tickets + 1 trustline
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);  // escrow was deleted
        BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);    // trustline is sponsored
        BEAST_EXPECT(
            sponsoringOwnerCount(env, sponsor) == 1);  // still sponsoring 1 object (trustline now)
    }

    void
    test2342EscrowCancelIouSameSponsorRecycleReserve()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2342
        // EscrowCancel same-sponsor reserve recycling when sender loses their trust line
        testcase("EscrowCancel IOU same-sponsor recycle reserve");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const gw{"gw"};
        Account const sponsor{"sponsor"};
        IOU const usd = gw["USD"];

        Env env{*this, testableAmendments()};
        env.fund(XRP(10000), alice, bob, gw, sponsor);
        env.close();
        auto const baseFee = env.current()->fees().base;

        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        auto const initialUSD = usd(1'000);
        auto const lockedUSD = usd(900);

        env(trust(alice, usd(2'000)));
        env.close();
        env(pay(gw, alice, initialUSD));
        env.close();

        auto const preAliceUSD = env.balance(alice, usd);
        BEAST_EXPECT(preAliceUSD == initialUSD);

        // Create escrow WITH CancelAfter so it can be cancelled
        std::uint32_t const seq = env.seq(alice);
        env(escrow::create(alice, bob, lockedUSD),
            escrow::kFinishTime(env.now() + std::chrono::seconds{100}),
            escrow::kCancelTime(env.now() + std::chrono::seconds{200}),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();
        BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD - lockedUSD);

        auto const escrowKeylet = keylet::escrow(alice, seq);
        auto sleEscrow = env.le(escrowKeylet);
        BEAST_EXPECT(sleEscrow != nullptr);
        if (!sleEscrow)
            return;

        BEAST_EXPECT(sleEscrow->getFieldAmount(sfAmount) == lockedUSD);
        BEAST_EXPECT(sleEscrow->isFieldPresent(sfSponsor));
        BEAST_EXPECT(sleEscrow->getAccountID(sfSponsor) == sponsor.id());
        BEAST_EXPECT(ownerCount(env, alice) == 2);  // trustline + escrow

        // Alice removes her trust line (pays back to issuer and sets limit to 0)
        env(pay(alice, gw, usd(100)));  // partial payment to reduce balance
        env.close();
        env(trust(alice, usd(0)));
        env.close();

        // Verify alice's trustline is gone
        BEAST_EXPECT(env.le(keylet::line(alice, usd.issue())) == nullptr);
        BEAST_EXPECT(ownerCount(env, alice) == 1);  // only escrow remains

        // Set sponsor to exactly afford the currently sponsored escrow
        adjustAccountXRPBalance(env, sponsor, reserve(env, 1));
        adjustAccountXRPBalance(env, alice, reserve(env, 1) + (baseFee * 10));
        BEAST_EXPECT(env.balance(sponsor) == reserve(env, 1));
        BEAST_EXPECT(env.balance(alice) < reserve(env, 2));

        // Wait for cancel time
        env.close(std::chrono::seconds{201});

        // FIX VERIFIED: Alice cancels with same sponsor, reserve recycling works
        // Escrow deleted, alice's trustline recreated with same sponsor
        env(escrow::cancel(alice, alice, seq),
            Fee(baseFee),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tesSUCCESS));
        env.close();

        // FIX: Escrow has been successfully cancelled
        sleEscrow = env.le(escrowKeylet);
        BEAST_EXPECT(sleEscrow == nullptr);

        // FIX: Alice got her escrowed funds back
        BEAST_EXPECT(env.balance(alice, usd) == lockedUSD);

        // FIX: Alice's trustline was recreated and is sponsored
        auto const aliceLine = env.le(keylet::line(alice, usd.issue()));
        BEAST_EXPECT(aliceLine != nullptr);
        if (aliceLine)
        {
            auto const& aliceSponsorField = alice.id() > gw.id() ? sfHighSponsor : sfLowSponsor;
            BEAST_EXPECT(aliceLine->isFieldPresent(aliceSponsorField));
            BEAST_EXPECT(aliceLine->getAccountID(aliceSponsorField) == sponsor.id());
        }

        // FIX: Owner counts - escrow deleted, trustline created
        BEAST_EXPECT(ownerCount(env, alice) == 1);              // alice's trustline
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);     // trustline is sponsored
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // still sponsoring 1 object
    }

    void
    test2342EscrowFinishMptSameSponsorRecycleReserve()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2342
        // EscrowFinish same-sponsor reserve recycling for MPT escrows

        // BUG: EscrowFinish can falsely reject a same-sponsor finish when the sponsor
        // funds both the escrow being deleted and the destination MPT holding being created.
        // The net reserve change is zero, but the check fires before the escrow deletion.
        testcase("EscrowFinish MPT same-sponsor recycle reserve");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const gw{"gw"};
        Account const sponsor{"sponsor"};

        Env env{*this, testableAmendments()};
        env.fund(XRP(10000), alice, bob, gw, sponsor);
        env.close();
        auto const baseFee = env.current()->fees().base;

        MPTTester mptTester{env, gw, {.holders = {alice}, .fund = false}};
        mptTester.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanEscrow});
        auto const mpt = mptTester["MPT"];
        auto const initialMPT = mpt(1'000);
        auto const lockedMPT = mpt(900);
        mptTester.authorize({.account = alice});
        mptTester.pay(gw, alice, 1'000);

        env(ticket::create(bob, 2));
        env.close();
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        auto const preAliceMPT = env.balance(alice, mpt);
        BEAST_EXPECT(preAliceMPT == initialMPT);

        std::uint32_t const seq = env.seq(alice);
        env(escrow::create(alice, bob, lockedMPT),
            escrow::kFinishTime(env.now() + std::chrono::seconds{1}),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();
        env.close(std::chrono::seconds{2});
        BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - lockedMPT);

        auto const escrowKeylet = keylet::escrow(alice, seq);
        auto sleEscrow = env.le(escrowKeylet);
        BEAST_EXPECT(sleEscrow != nullptr);
        if (!sleEscrow)
            return;

        BEAST_EXPECT(sleEscrow->getFieldAmount(sfAmount) == lockedMPT);
        BEAST_EXPECT(sleEscrow->isFieldPresent(sfSponsor));
        BEAST_EXPECT(sleEscrow->getAccountID(sfSponsor) == sponsor.id());
        BEAST_EXPECT(!sleEscrow->isFieldPresent(sfCancelAfter));
        BEAST_EXPECT(ownerCount(env, alice) == 2);
        BEAST_EXPECT(ownerCount(env, bob) == 2);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // Same-sponsor finish should be reserve-neutral: the existing sponsored
        // Escrow is deleted as Bob's destination MPToken is created.
        adjustAccountXRPBalance(env, sponsor, reserve(env, 1));
        adjustAccountXRPBalance(env, bob, reserve(env, 2) + (baseFee * 10));
        BEAST_EXPECT(env.balance(sponsor) == reserve(env, 1));
        BEAST_EXPECT(env.balance(bob) < reserve(env, 3));

        // Without sponsor, fails with tecINSUFFICIENT_RESERVE (expected - bob can't self-fund)
        env(escrow::finish(bob, alice, seq), Fee(baseFee), Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // FIX VERIFIED: With same sponsor, succeeds (net-zero reserve change)
        // The sponsor recycles reserve from escrow deletion to MPToken holding creation
        env(escrow::finish(bob, alice, seq),
            Fee(baseFee),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tesSUCCESS));
        env.close();

        // FIX: Escrow has been successfully finished
        sleEscrow = env.le(escrowKeylet);
        BEAST_EXPECT(sleEscrow == nullptr);

        // FIX: Bob received the escrowed MPT
        BEAST_EXPECT(env.balance(bob, mpt) == lockedMPT);
        BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - lockedMPT);

        // FIX: Bob's MPToken holding was created and is sponsored
        auto const bobMpt = env.le(keylet::mptoken(mpt.issuanceID, bob.id()));
        BEAST_EXPECT(bobMpt != nullptr);
        if (bobMpt)
        {
            BEAST_EXPECT(bobMpt->isFieldPresent(sfSponsor));
            BEAST_EXPECT(bobMpt->getAccountID(sfSponsor) == sponsor.id());
        }

        // FIX: Owner counts updated correctly (escrow deleted, MPToken holding created)
        BEAST_EXPECT(ownerCount(env, alice) == 1);           // only alice's MPToken holding remains
        BEAST_EXPECT(ownerCount(env, bob) == 3);             // 2 tickets + 1 MPToken holding
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);  // escrow was deleted
        BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);    // MPToken holding is sponsored
        BEAST_EXPECT(
            sponsoringOwnerCount(env, sponsor) == 1);  // still sponsoring 1 object (MPToken now)
    }

    void
    test2671TicketCoSignedWithoutSponsorshipInflatesReserveCount()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2671
        // Co-signed TicketCreate without Sponsorship object inflates ReserveCount on consumption
        testcase(
            "ReserveCount inflation: co-signed TicketCreate + later Sponsorship + ticket "
            "consumption");

        using namespace jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // Precondition: no Sponsorship object exists between sponsor and alice
        BEAST_EXPECT(!env.le(keylet::sponsorship(sponsor, alice)));
        BEAST_EXPECT(env.sponsoredOwnerCount(alice) == 0);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == 0);

        // Step 1: sponsor co-signs TicketCreate for alice (no Sponsorship SLE exists)
        std::uint32_t const ticketCount = 10;
        std::uint32_t const firstTicketSeq = env.seq(alice) + 1;

        env(ticket::create(alice, ticketCount),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor));
        env.close();

        // Each ticket carries sfSponsor = sponsor
        for (std::uint32_t i = 0; i < ticketCount; ++i)
        {
            auto const sleTicket = env.le(keylet::kTicket(alice, firstTicketSeq + i));
            BEAST_EXPECT(sleTicket);
            if (sleTicket)
                BEAST_EXPECT(sleTicket->getAccountID(sfSponsor) == sponsor.id());
        }

        BEAST_EXPECT(env.sponsoredOwnerCount(alice) == ticketCount);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == ticketCount);

        // Crucially: still no Sponsorship object, so no ReserveCount was debited at create time
        BEAST_EXPECT(!env.le(keylet::sponsorship(sponsor, alice)));

        // Step 2: sponsor later creates a Sponsorship object with alice
        // We do NOT pre-fund any ReserveCount (omitted field = semantically 0)
        env(sponsor::set(sponsor, 0, std::nullopt, XRP(1)), sponsor::SponseeAcc(alice));
        env.close();

        auto sleSponsorship = env.le(keylet::sponsorship(sponsor, alice));
        BEAST_EXPECT(sleSponsorship);
        BEAST_EXPECT(!sleSponsorship->isFieldPresent(sfRemainingOwnerCount));

        // Step 3: alice consumes all tickets
        for (std::uint32_t i = 0; i < ticketCount; ++i)
        {
            env(noop(alice), ticket::Use(firstTicketSeq + i));
            env.close();
        }

        // FIX VERIFIED: Sponsorship.sfRemainingOwnerCount should still be 0 (or absent)
        // BUG WOULD BE: it gets inflated to ticketCount
        sleSponsorship = env.le(keylet::sponsorship(sponsor, alice));
        BEAST_EXPECT(sleSponsorship);

        // Verify ReserveCount was NOT inflated (correct behavior)
        if (sleSponsorship && sleSponsorship->isFieldPresent(sfRemainingOwnerCount))
        {
            auto const reserveCount = sleSponsorship->getFieldU32(sfRemainingOwnerCount);
            // Should be 0, not ticketCount
            BEAST_EXPECT(reserveCount == 0);
        }
        else
        {
            // Field absent is also correct (semantically 0)
            // This is the expected fix behavior
        }

        // Owner counts should be back to zero after consuming all tickets
        BEAST_EXPECT(env.sponsoredOwnerCount(alice) == 0);
        BEAST_EXPECT(env.sponsoringOwnerCount(sponsor) == 0);
    }

    void
    test2721ZeroBalanceSponsoredPaymentFeePayerCheck()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2721
        // Zero-balance sponsored Payment: getFeePayer() consistency check
        testcase("Sponsored Payment: minimal-balance account with sponsor-pays-fee");

        using namespace jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const dest("dest");

        auto const baseFee = drops(10);
        auto const baseReserve = env.current()->fees().reserve;

        // Fund sponsor and dest generously, alice with base reserve + 1 XRP for payment
        env.fund(XRP(10000), sponsor, dest);
        env.fund(baseReserve + XRP(1), alice);
        env.close();

        // Precondition: alice has base reserve + 1 XRP (enough for payment but not fee)
        BEAST_EXPECT(env.balance(alice) == baseReserve + XRP(1));

        // BUG SCENARIO (if it existed): Alice tries to send a Payment to dest
        // where sponsor pays the fee via spfSponsorFee.
        // If Payment.cpp used ctx_.tx.getFeePayer() (STTx version), it would
        // incorrectly identify alice as the fee payer and check if alice has
        // balance >= amount + fee + reserve, which would fail.
        // FIX: Payment.cpp uses getFeePayer(view(), ctx_.tx) (Transactor version)
        // which correctly identifies sponsor as the fee payer, so only checks
        // if alice has balance >= amount + reserve (not including fee).

        auto const preDest = env.balance(dest);
        auto const preSponsor = env.balance(sponsor);

        // Alice sends 1 XRP to dest, sponsor pays the fee
        env(pay(alice, dest, XRP(1)),
            sponsor::As(sponsor, spfSponsorFee),
            Sig(sfSponsorSignature, sponsor),
            Fee(baseFee),
            Ter(tesSUCCESS));
        env.close();

        // FIX VERIFIED: Payment succeeded
        // Alice's balance decreased by 1 XRP (the payment amount, NOT the fee)
        BEAST_EXPECT(env.balance(alice) == baseReserve);

        // Dest received 1 XRP
        BEAST_EXPECT(env.balance(dest) == preDest + XRP(1));

        // Sponsor paid the fee (NOT alice)
        BEAST_EXPECT(env.balance(sponsor) == preSponsor - baseFee);
    }

    void
    test2731OracleSetDropsSponsorSwitchWhenReserveUnchanged()
    {
        // https://github.com/sherlock-audit/2026-04-xrp-ledger-april-2026-judging/issues/2731
        // OracleSet sponsor switch when reserve count unchanged
        // UPDATE: NOT A BUG. If  someone want to change sponsor without increasing oracle pairs
        // count they must use SponsorshipTransfer.
        testcase("OracleSet: sponsor switch dropped when reserve count unchanged");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, testableAmendments()};

        Account const owner("owner");
        Account const sponsorA("sponsorA");
        Account const sponsorB("sponsorB");

        env.fund(XRP(10000), owner, sponsorA, sponsorB);
        env.close();

        // Both sponsors pre-fund reserves for the owner
        env(sponsor::set(sponsorA, 0, 5, std::nullopt, std::nullopt), sponsor::SponseeAcc(owner));
        env(sponsor::set(sponsorB, 0, 5, std::nullopt, std::nullopt), sponsor::SponseeAcc(owner));
        env.close();

        // Create an oracle with sponsorA as the reserve sponsor
        // Use 1 price pair (reserve count = 1 based on calculateOracleReserve)
        auto const now = env.timeKeeper().now();
        env.close(now + oracle::kTestStartTime - kEpochOffset);

        json::Value createTx;
        createTx[jss::TransactionType] = jss::OracleSet;
        createTx[jss::Account] = to_string(owner);
        createTx[jss::OracleDocumentID] = 1;
        createTx[jss::Provider] = strHex(std::string("provider"));
        createTx[jss::AssetClass] = strHex(std::string("currency"));
        createTx[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(env.current()->header().closeTime.time_since_epoch()).count() +
            kEpochOffset.count() + 100);

        json::Value priceData(json::ValueType::Array);
        json::Value pd;
        pd[jss::PriceData][jss::BaseAsset] = "XRP";
        pd[jss::PriceData][jss::QuoteAsset] = "USD";
        pd[jss::PriceData][jss::AssetPrice] = 1000;
        pd[jss::PriceData][jss::Scale] = 1;
        priceData.append(pd);
        createTx[jss::PriceDataSeries] = priceData;

        env(createTx, sponsor::As(sponsorA, spfSponsorReserve));
        env.close();

        // Verify oracle exists and sfSponsor == sponsorA
        auto const oracleKey = keylet::oracle(owner, 1);
        auto oracleLE = env.le(oracleKey);
        BEAST_EXPECT(oracleLE);
        if (oracleLE)
        {
            BEAST_EXPECT(oracleLE->isFieldPresent(sfSponsor));
            if (oracleLE->isFieldPresent(sfSponsor))
            {
                BEAST_EXPECT(oracleLE->getAccountID(sfSponsor) == sponsorA.id());
            }
        }

        // Update oracle: same price pair count (reserve unchanged), but with sponsorB
        // BUG: sponsor switch is dropped because adjust == 0
        // UPDATE: not a bug, someone always can use TransferSponsorship
        env.close(now + oracle::kTestStartTime - kEpochOffset + seconds(1));
        createTx[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(env.current()->header().closeTime.time_since_epoch()).count() +
            kEpochOffset.count() + 100);

        env(createTx, sponsor::As(sponsorB, spfSponsorReserve));
        env.close();

        // Check if bug is present: Oracle should show sponsorB but will show sponsorA if bug exists
        oracleLE = env.le(oracleKey);
        BEAST_EXPECT(oracleLE);
        if (oracleLE)
        {
            BEAST_EXPECT(oracleLE->isFieldPresent(sfSponsor));
            if (oracleLE->isFieldPresent(sfSponsor))
            {
                auto const actualSponsor = oracleLE->getAccountID(sfSponsor);
                // BUG: Sponsor switch is silently dropped when adjust == 0
                // The sfSponsor remains sponsorA instead of switching to sponsorB
                // UPDATE: Not A Bug - One should use SponsorshipTransfer in this case.
                BEAST_EXPECT(actualSponsor == sponsorA.id());
            }
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;

        test168CoSignedBlockedWithFeeOnlySponsorship();
        test251AMMDepositRejectXRPDeposits();
        test750SponsorFeeQueueAdmissionBug();
        test750AdversarialSponsorBlocksVictim();
        test1033SponsoredWitnessCanChargeDoorOwnedClaimObjectsToUnrelatedSponsor();
        test1186AMMCreateUsesPreFeeReserveBalance();
        test1186AMMDepositUsesPreFeeReserveBalance();
        test1350ReserveCountSilentWrap(testableAmendments());
        test1364AmmWithdrawSponsoredMptBypass();
        test1365OracleReserveDecreaseRejection();
        test1380AmmClawbackReserveBypass();
        test1468PathPaymentExploit();
        test1563OracleIncorrectAdjustment();
        test1675SponsoredXRPEscrowCreate();
        test1678SameSponsorCredentialAccept();
        test1680SponsoredPayChanTrapsReserve();
        test1736CrossCurrencyTfSponsorCreatedAccountBypassesReserve();
        test1779BrokerSponsorMisroutedToBorrowerLoanSle();
        test1814AMMDepositLPTokenNonSponsoredReserveBypass();
        test1814ExistingLPCorrectlyChecked();
        test1814SingleAssetCaughtByPreclaim();
        test2022UnsignedUnderflowAccountReserveOfferCrossing();
        test2065SponsorVaultFeeInvariantDeposit();
        test2065SponsorVaultFeeInvariantWithdraw();
        test2158SponsorLoanBrokerSetMPTPseudoAccountInvariant();
        test2178TrustSetCounterpartySponsorMisroute();
        test2241AlternateFeePayerQueueAdmissionDelegate();
        test2241AlternateFeePayerQueueAdmissionSponsored();
        test2284LegacySignerListReserveMismatch();
        test2320LoanSetBorrowerSponsorNotAppliedToLenderTrustline();
        test2320LoanSetBorrowerSponsorNotAppliedToLenderMptHolding();
        test2320LoanSetBorrowerSponsorOnlySponsorsIntendedBorrowerLoan();
        test2320BorrowerSponsorReserveLockScalesAcrossManyLenders();
        test2342EscrowFinishIouSameSponsorRecycleReserve();
        test2342EscrowCancelIouSameSponsorRecycleReserve();
        test2342EscrowFinishMptSameSponsorRecycleReserve();
        test2671TicketCoSignedWithoutSponsorshipInflatesReserveCount();
        test2721ZeroBalanceSponsoredPaymentFeePayerCheck();
        test2731OracleSetDropsSponsorSwitchWhenReserveUnchanged();
    }
};

BEAST_DEFINE_TESTSUITE(SponsorSherlock, app, xrpl);

}  // namespace xrpl::test
