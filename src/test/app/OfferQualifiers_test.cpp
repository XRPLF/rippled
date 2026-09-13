#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

// Tests for featureOfferQualifiers: all-or-none (tfAllOrNone), minimum
// quantity (sfMinQuantity), and strict post-only (tfPostOnly).
class OfferQualifiers_test : public beast::unit_test::Suite
{
    // Read the flags of an account's offer at a given sequence.
    static std::uint32_t
    offerFlags(jtx::Env& env, jtx::Account const& acct, std::uint32_t seq)
    {
        auto const sle = env.le(keylet::offer(acct.id(), SeqProxy::rawSequence(seq)));
        return sle ? sle->getFieldU32(sfFlags) : 0;
    }

    // ---- Group A: preflight validation ----

    void
    testAmendmentGate()
    {
        testcase("amendment gate");
        using namespace jtx;
        Env env{*this, testableAmendments() - featureOfferQualifiers};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(USD(10'000), alice);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        // The qualifier flags are not allowed without the amendment.
        env(offer(alice, XRP(100), USD(100), tfAllOrNone), Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), USD(100), tfPostOnly), Ter(temINVALID_FLAG));
    }

    void
    testFlagCombos()
    {
        testcase("invalid flag combinations");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(USD(10'000), alice);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        // AllOrNone is immediate-or-rest, never immediate-or-cancel; combining
        // with IoC/FoK is malformed (immediate-AON is exactly FoK).
        env(offer(alice, XRP(100), USD(100), tfAllOrNone | tfFillOrKill), Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), USD(100), tfAllOrNone | tfImmediateOrCancel),
            Ter(temINVALID_FLAG));

        // Post-only never removes liquidity; it cannot combine with flags that
        // require taking it.
        env(offer(alice, XRP(100), USD(100), tfPostOnly | tfImmediateOrCancel),
            Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), USD(100), tfPostOnly | tfFillOrKill), Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), USD(100), tfPostOnly | tfSell), Ter(temINVALID_FLAG));
    }

    // ---- Group B: AON entry (fill-whole-or-rest-whole) ----

    void
    testAonEntryRestsWholeWhenNoLiquidity()
    {
        testcase("AON rests whole with no liquidity");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(USD(10'000), alice);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        auto const seq = env.seq(alice);
        env(offer(alice, XRP(100), USD(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1));
        // The resting offer carries the all-or-none ledger flag.
        BEAST_EXPECT(offerFlags(env, alice, seq) & lsfAllOrNone);
    }

    void
    testAonEntryFullyCrosses()
    {
        testcase("AON fully crosses when liquidity suffices");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // bob rests generous liquidity: gives 100 XRP, wants only 80 USD.
        env(offer(bob, USD(80), XRP(100)));
        env.close();

        // alice's AON wants 100 XRP for 100 USD: fully crosses, nothing rests.
        env(offer(alice, XRP(100), USD(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 0), offers(bob, 0));
    }

    void
    testAonEntryNoPartialFill()
    {
        testcase("AON never partially fills on entry");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // Only 50 XRP of (clearly marketable) liquidity, less than alice's 100.
        env(offer(bob, USD(40), XRP(50)));
        env.close();

        // alice's AON cannot fully fill, so it crosses nothing and rests whole.
        auto const aliceBefore = env.balance(alice, USD);
        env(offer(alice, XRP(100), USD(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(bob, 1));
        // No funds moved: alice paid no USD.
        BEAST_EXPECT(env.balance(alice, USD) == aliceBefore);
    }

    // ---- Group C: strict post-only ----

    void
    testPostOnlyRejectsMarketable()
    {
        testcase("post-only rejects a marketable offer");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // bob rests clearly-crossable liquidity (100 XRP for 80 USD).
        env(offer(bob, USD(80), XRP(100)));
        env.close();

        // alice's post-only would cross bob, so it is rejected and not placed.
        env(offer(alice, XRP(100), USD(100), tfPostOnly), Ter(tecWOULD_CROSS));
        env.close();
        env.require(offers(alice, 0), offers(bob, 1));
    }

    void
    testPostOnlyRestsWhenPassive()
    {
        testcase("post-only rests when non-marketable");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // bob wants 120 USD for 100 XRP — clearly worse than alice will pay.
        env(offer(bob, USD(120), XRP(100)));
        env.close();

        // alice pays only 100 USD for 100 XRP, so she does not cross bob: rests.
        env(offer(alice, XRP(100), USD(100), tfPostOnly), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(bob, 1));
    }

    // ---- Group D: consumption ----
    //
    // A resting AON offer must be consumed in full or not at all. A taker whose
    // demand is smaller than the AON offer's size must trade through it (leave
    // it untouched), never partially consume it.
    void
    testRestingAonSkippedBySmallTaker()
    {
        testcase("resting AON is skipped by a too-small taker");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // alice rests a 100-XRP AON offer (gives 100 XRP, wants 100 USD).
        env(offer(alice, USD(100), XRP(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1));

        // bob only wants 50 XRP — less than alice's all-or-none size, but at a
        // clearly-marketable price (60 USD). He must not partially consume her
        // offer; both rest untouched.
        env(offer(bob, XRP(50), USD(60)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(bob, 1));
    }

    // A taker must trade *through* a too-big AON at the best price to reach a
    // worse-priced divisible offer (validates the ofrQ-reset / no-deadlock).
    void
    testTradeThroughAonToWorseOffer()
    {
        testcase("trade through a too-big AON to a worse offer");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const carol = Account{"carol"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, carol, bob);
        env.close();
        env.trust(USD(10'000), alice, carol, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, carol, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // alice's AON sits at the best price (100 XRP for 100 USD = 1.0).
        env(offer(alice, USD(100), XRP(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        // carol rests a worse-priced divisible offer (50 XRP for 55 USD = 1.1).
        env(offer(carol, USD(55), XRP(50)), Ter(tesSUCCESS));
        env.close();

        auto const aliceBefore = env.balance(alice, USD);
        // bob wants 50 XRP and will pay up to 120 USD. He cannot take alice's
        // 100-XRP AON in full, so he trades through it and takes carol's 50.
        env(offer(bob, XRP(50), USD(120)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(carol, 0), offers(bob, 0));
        // alice's AON was untouched (no funds moved).
        BEAST_EXPECT(env.balance(alice, USD) == aliceBefore);
    }

    // Build an OfferCreate carrying sfMinQuantity (denominated in TakerGets).
    static json::Value
    offerWithMinQty(
        jtx::Account const& account,
        STAmount const& takerPays,
        STAmount const& takerGets,
        STAmount const& minQty)
    {
        auto jv = jtx::offer(account, takerPays, takerGets);
        jv[sfMinQuantity.jsonName] = minQty.getJson(JsonOptions::Values::None);
        return jv;
    }

    // ---- Group E: MinQty preflight ----

    void
    testMinQtyPreflight()
    {
        testcase("MinQuantity preflight");
        using namespace jtx;
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];

        {
            // The field is not allowed without the amendment.
            Env env{*this, testableAmendments() - featureOfferQualifiers};
            env.fund(XRP(10'000), gw, alice);
            env.close();
            env(offerWithMinQty(alice, USD(100), XRP(100), XRP(50)), Ter(temDISABLED));
        }

        Env env{*this, testableAmendments()};
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(USD(10'000), alice);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        // AllOrNone is MinQuantity pinned to the full size; both is malformed.
        {
            auto jv = offerWithMinQty(alice, USD(100), XRP(100), XRP(50));
            jv[jss::Flags] = tfAllOrNone;
            env(jv, Ter(temMALFORMED));
        }
        // The floor must be denominated in the TakerGets asset.
        env(offerWithMinQty(alice, USD(100), XRP(100), USD(50)), Ter(temMALFORMED));
        // The floor must be positive and no larger than TakerGets.
        env(offerWithMinQty(alice, USD(100), XRP(100), XRP(0)), Ter(temMALFORMED));
        env(offerWithMinQty(alice, USD(100), XRP(100), XRP(101)), Ter(temMALFORMED));
    }

    // ---- Group F: MinQty entry (cross at least the floor or rest whole) ----

    void
    testMinQtyEntryRestsWholeBelowFloor()
    {
        testcase("MinQty entry rests whole when below-floor crosses");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // bob's resting offer can absorb only 30 XRP of alice's 100.
        env(offer(bob, XRP(30), USD(30)), Ter(tesSUCCESS));
        env.close();

        auto const aliceUsdBefore = env.balance(alice, USD);
        auto const seq = env.seq(alice);
        // alice gives 100 XRP with a 50-XRP floor: only 30 is immediately
        // obtainable, so nothing executes and the whole offer rests.
        env(offerWithMinQty(alice, USD(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        env.require(offers(alice, 1), offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, USD) == aliceUsdBefore);
        auto const sle = env.le(keylet::offer(alice.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
        {
            BEAST_EXPECT(sle->getFieldAmount(sfTakerGets) == XRP(100));
            BEAST_EXPECT(sle->getFieldAmount(sfMinQuantity) == XRP(50));
        }
    }

    void
    testMinQtyEntryPartialFillAboveFloor()
    {
        testcase("MinQty entry partial-fills above the floor");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // bob's resting offer absorbs 60 XRP — above alice's 50-XRP floor.
        env(offer(bob, XRP(60), USD(60)), Ter(tesSUCCESS));
        env.close();

        auto const seq = env.seq(alice);
        env(offerWithMinQty(alice, USD(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        // 60 crossed; the 40-XRP remainder rests, still carrying the floor.
        env.require(offers(alice, 1), offers(bob, 0));
        BEAST_EXPECT(env.balance(alice, USD) == USD(1'060));
        auto const sle = env.le(keylet::offer(alice.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
        {
            BEAST_EXPECT(sle->getFieldAmount(sfTakerGets) == XRP(40));
            BEAST_EXPECT(sle->getFieldAmount(sfMinQuantity) == XRP(50));
        }
    }

    // ---- Group G: MinQty resting consumption ----

    void
    testRestingMinQtySkippedBelowFloor()
    {
        testcase("resting MinQty is skipped by a below-floor taker");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // alice rests 100 XRP with a 50-XRP floor.
        env(offerWithMinQty(alice, USD(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        // bob wants only 30 XRP at a marketable price: below the floor, so
        // alice's offer is skipped and bob's offer rests.
        env(offer(bob, XRP(30), USD(36)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, USD) == USD(1'000));
    }

    void
    testRestingMinQtyFillsAtFloorAndRemainderKeepsFloor()
    {
        testcase("resting MinQty fills at/above floor; remainder keeps floor");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const dan = Account{"dan"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob, carol, dan);
        env.close();
        env.trust(USD(10'000), alice, bob, carol, dan);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));
        env(pay(gw, carol, USD(1'000)));
        env(pay(gw, dan, USD(1'000)));
        env.close();

        auto const seq = env.seq(alice);
        env(offerWithMinQty(alice, USD(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        // bob takes exactly the floor: fills 50, remainder 50 rests.
        env(offer(bob, XRP(50), USD(50)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(bob, 0));
        BEAST_EXPECT(env.balance(alice, USD) == USD(1'050));
        auto const sle = env.le(keylet::offer(alice.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
        {
            BEAST_EXPECT(sle->getFieldAmount(sfTakerGets) == XRP(50));
            BEAST_EXPECT(sle->getFieldAmount(sfMinQuantity) == XRP(50));
        }

        // carol wants 30 — below the remainder's floor (min(50, 50)): skipped.
        env(offer(carol, XRP(30), USD(36)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1), offers(carol, 1));
        BEAST_EXPECT(env.balance(alice, USD) == USD(1'050));

        // dan takes the whole 50-XRP remainder: the offer is consumed. (His
        // demand also sweeps carol's resting 30-XRP bid first, so he asks for
        // 80 in total.)
        env(offer(dan, XRP(80), USD(96)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 0));
        BEAST_EXPECT(env.balance(alice, USD) == USD(1'100));
    }

    // An under-funded resting AON offer can never deliver its full size, so a
    // crosser reaps it (like any unfunded offer) rather than leaving it to
    // block the book. This is the anti-DoS property: a partially-funded
    // contingent offer is not kept forever.
    void
    testRestingAonUnderfundedIsReaped()
    {
        testcase("under-funded resting AON is reaped, not kept");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        // alice holds only 50 USD but posts a 100-USD all-or-none offer:
        // partially funded, so it survives the zero-funded reap but can never
        // satisfy its own all-or-none size.
        env(pay(gw, alice, USD(50)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        env(offer(alice, XRP(100), USD(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1));

        // bob's demand (100 USD) covers alice's full size, but she cannot
        // deliver it. Her offer is reaped; bob crosses nothing and rests.
        env(offer(bob, USD(100), XRP(100)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 0), offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, USD) == USD(50));
    }

    void
    testRestingMinQtyUnderfundedIsReaped()
    {
        testcase("under-funded resting MinQty is reaped, not kept");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        // alice holds 30 USD, below her offer's 50-USD floor.
        env(pay(gw, alice, USD(30)));
        env(pay(gw, bob, USD(1'000)));
        env.close();

        env(offerWithMinQty(alice, XRP(100), USD(100), USD(50)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1));

        // alice can fund only 30 USD — below her own floor — so no fill can
        // satisfy it. The crosser reaps it.
        env(offer(bob, USD(100), XRP(100)), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 0), offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, USD) == USD(30));
    }

    // ---- Group H: security / engine-scope interactions ----

    void
    testPostOnlyIgnoresUnfundedSpoof()
    {
        testcase("post-only ignores unfunded spoof offers");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const carol = Account{"carol"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, carol);
        env.close();
        env.trust(USD(10'000), alice, carol);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, carol, USD(100)));
        env.close();

        // carol spoofs the book: she rests a better-priced offer giving USD,
        // then moves the USD away, leaving the offer unfunded.
        env(offer(carol, XRP(50), USD(100)), Ter(tesSUCCESS));
        env.close();
        env(pay(carol, gw, USD(100)));
        env.close();

        // alice's post-only would cross carol's price — but only against
        // funded liquidity. The spoof is inert; alice's offer rests.
        env(offer(alice, USD(100), XRP(100), tfPostOnly), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1));
    }

    void
    testAonInvisibleToPayment()
    {
        testcase("contingent offers are invisible to payment strands");
        using namespace jtx;
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const USD = gw["USD"];

        auto setup = [&](Env& env) {
            env.fund(XRP(10'000), gw, alice, bob, carol);
            env.close();
            env.trust(USD(10'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(1'000)));
            env(pay(gw, bob, USD(1'000)));
            env.close();
        };

        {
            // Control: a plain resting offer routes bob's cross-currency
            // payment (bob pays USD, carol receives XRP via alice's offer).
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(alice, USD(100), XRP(100)), Ter(tesSUCCESS));
            env.close();
            env(pay(bob, carol, XRP(50)), Sendmax(USD(60)), Ter(tesSUCCESS));
            env.close();
        }
        {
            // The same book with an AON offer: a payment strand never
            // consumes contingent offers, even when the size would fit.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(alice, USD(100), XRP(100), tfAllOrNone), Ter(tesSUCCESS));
            env.close();
            env(pay(bob, carol, XRP(50)), Sendmax(USD(60)), Ter(tecPATH_PARTIAL));
            env.close();
            env.require(offers(alice, 1));
        }
    }

    void
    testSellAonEntry()
    {
        testcase("tfSell composes with AON");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(USD(10'000), alice);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        // With no liquidity a sell-AON rests whole, carrying both flags.
        auto const seq = env.seq(alice);
        env(offer(alice, XRP(100), USD(100), tfAllOrNone | tfSell), Ter(tesSUCCESS));
        env.close();
        env.require(offers(alice, 1));
        auto const flags = offerFlags(env, alice, seq);
        BEAST_EXPECT(flags & lsfAllOrNone);
        BEAST_EXPECT(flags & lsfSell);
    }

    void
    testAonCrossesAmm()
    {
        testcase("AON crosses AMM liquidity; too-big AON rests whole");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(1'000'000), gw, alice, bob);
        env.close();
        env.trust(USD(1'000'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(100'000)));
        env(pay(gw, bob, USD(100'000)));
        env.close();

        // A deep pool: 100k XRP / 100k USD, mid price 1.0.
        AMM amm(env, alice, XRP(100'000), USD(100'000));

        // bob's small AON is fully satisfiable from the pool: it crosses in
        // full and nothing rests.
        env(offer(bob, XRP(100), USD(102), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(bob, 0));

        // bob's over-priced AON cannot fully cross within its limit quality:
        // it executes nothing and rests whole.
        auto const seq = env.seq(bob);
        env(offer(bob, XRP(50'000), USD(50'000), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(offers(bob, 1));
        auto const sle = env.le(keylet::offer(bob.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
            BEAST_EXPECT(sle->getFieldAmount(sfTakerGets) == USD(50'000));
    }

    // A MinQty offer sitting in one leg of an autobridged (IOU→XRP→IOU)
    // offer-crossing strand: it is consumed through a multi-step strand (the
    // forward pass), which is the highest-risk path for a sub-floor rounding
    // fill. Confirm it either fills at/above its floor or is skipped whole —
    // never partially consumed below the floor (which would trip
    // ValidContingentOffers on an innocent taker).
    void
    testMinQtyAutobridge()
    {
        testcase("MinQty offer in an autobridged crossing leg");
        using namespace jtx;
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto setup = [&](Env& env) {
            env.fund(XRP(100'000), gw, alice, bob);
            env.close();
            env.trust(USD(100'000), alice, bob);
            env.trust(EUR(100'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, alice, EUR(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();
            // Bridge legs, both 1:1: USD→XRP and XRP→EUR. The XRP→EUR leg
            // carries a 50-EUR minimum. No direct USD/EUR book, so bob can
            // only fill by autobridging through XRP.
            env(offer(alice, USD(100), XRP(100)), Ter(tesSUCCESS));
            env(offerWithMinQty(alice, XRP(100), EUR(100), EUR(50)), Ter(tesSUCCESS));
            env.close();
        };

        {
            // bob wants 100 EUR — above the 50 floor — so the bridge fully
            // engages and the MinQty leg is consumed in full.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(bob, EUR(100), USD(100)), Ter(tesSUCCESS));
            env.close();
            env.require(offers(bob, 0));
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(100));
        }
        {
            // bob wants only 30 EUR — below the floor — so the MinQty leg is
            // skipped, the bridge cannot deliver, and bob's offer rests
            // untouched. alice's legs remain; no sub-floor fill occurs.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(bob, EUR(30), USD(30)), Ter(tesSUCCESS));
            env.close();
            env.require(offers(bob, 1), offers(alice, 2));
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(0));
        }
        {
            // bob wants exactly 50 EUR — the floor boundary — the leg is
            // consumed to exactly its floor and the remainder rests.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(bob, EUR(50), USD(50)), Ter(tesSUCCESS));
            env.close();
            env.require(offers(bob, 0));
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(50));
        }
    }

    // ---- Group I: RPC ----

    void
    testBookOffersMarkers()
    {
        testcase("book_offers marks contingent entries");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const carol = Account{"carol"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, carol);
        env.close();
        env.trust(USD(10'000), alice, carol);
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env(pay(gw, carol, USD(1'000)));
        env.close();

        // Both give USD and want XRP: same book, distinct qualifiers.
        env(offer(alice, XRP(100), USD(100), tfAllOrNone), Ter(tesSUCCESS));
        env(offerWithMinQty(carol, XRP(100), USD(100), USD(40)), Ter(tesSUCCESS));
        env.close();

        // book_offers for the USD-out book (taker pays XRP, gets USD).
        json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::taker_pays][jss::currency] = "XRP";
        jvParams[jss::taker_gets][jss::currency] = "USD";
        jvParams[jss::taker_gets][jss::issuer] = gw.human();
        auto const result = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];

        if (BEAST_EXPECT(result.isMember(jss::offers) && result[jss::offers].size() == 2))
        {
            bool sawAon = false;
            bool sawMinQty = false;
            for (auto const& jvOffer : result[jss::offers])
            {
                if (jvOffer.isMember(jss::all_or_none) && jvOffer[jss::all_or_none] == true)
                    sawAon = true;
                if (jvOffer.isMember(jss::min_quantity))
                    sawMinQty = true;
            }
            BEAST_EXPECT(sawAon);
            BEAST_EXPECT(sawMinQty);
        }
    }

public:
    void
    run() override
    {
        testAmendmentGate();
        testFlagCombos();
        testAonEntryRestsWholeWhenNoLiquidity();
        testAonEntryFullyCrosses();
        testAonEntryNoPartialFill();
        testPostOnlyRejectsMarketable();
        testPostOnlyRestsWhenPassive();
        testRestingAonSkippedBySmallTaker();
        testTradeThroughAonToWorseOffer();
        testRestingAonUnderfundedIsReaped();
        testRestingMinQtyUnderfundedIsReaped();
        testMinQtyPreflight();
        testMinQtyEntryRestsWholeBelowFloor();
        testMinQtyEntryPartialFillAboveFloor();
        testRestingMinQtySkippedBelowFloor();
        testRestingMinQtyFillsAtFloorAndRemainderKeepsFloor();
        testPostOnlyIgnoresUnfundedSpoof();
        testAonInvisibleToPayment();
        testSellAonEntry();
        testAonCrossesAmm();
        testMinQtyAutobridge();
        testBookOffersMarkers();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(OfferQualifiers, app, xrpl, 2);

}  // namespace xrpl::test
