#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>

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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(10'000), alice);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // The qualifier flags are not allowed without the amendment.
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), usd(100), tfPostOnly), Ter(temINVALID_FLAG));
    }

    void
    testFlagCombos()
    {
        testcase("invalid flag combinations");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(10'000), alice);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // AllOrNone is immediate-or-rest, never immediate-or-cancel; combining
        // with IoC/FoK is malformed (immediate-AON is exactly FoK).
        env(offer(alice, XRP(100), usd(100), tfAllOrNone | tfFillOrKill), Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), usd(100), tfAllOrNone | tfImmediateOrCancel),
            Ter(temINVALID_FLAG));

        // Post-only never removes liquidity; it cannot combine with flags that
        // require taking it.
        env(offer(alice, XRP(100), usd(100), tfPostOnly | tfImmediateOrCancel),
            Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), usd(100), tfPostOnly | tfFillOrKill), Ter(temINVALID_FLAG));
        env(offer(alice, XRP(100), usd(100), tfPostOnly | tfSell), Ter(temINVALID_FLAG));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(10'000), alice);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        auto const seq = env.seq(alice);
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // bob rests generous liquidity: gives 100 XRP, wants only 80 USD.
        env(offer(bob, usd(80), XRP(100)));
        env.close();

        // alice's AON wants 100 XRP for 100 USD: fully crosses, nothing rests.
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 0), Offers(bob, 0));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // Only 50 XRP of (clearly marketable) liquidity, less than alice's 100.
        env(offer(bob, usd(40), XRP(50)));
        env.close();

        // alice's AON cannot fully fill, so it crosses nothing and rests whole.
        auto const aliceBefore = env.balance(alice, usd);
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(bob, 1));
        // No funds moved: alice paid no USD.
        BEAST_EXPECT(env.balance(alice, usd) == aliceBefore);
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // bob rests clearly-crossable liquidity (100 XRP for 80 USD).
        env(offer(bob, usd(80), XRP(100)));
        env.close();

        // alice's post-only would cross bob, so it is rejected and not placed.
        env(offer(alice, XRP(100), usd(100), tfPostOnly), Ter(tecWOULD_CROSS));
        env.close();
        env.require(Offers(alice, 0), Offers(bob, 1));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // bob wants 120 USD for 100 XRP — clearly worse than alice will pay.
        env(offer(bob, usd(120), XRP(100)));
        env.close();

        // alice pays only 100 USD for 100 XRP, so she does not cross bob: rests.
        env(offer(alice, XRP(100), usd(100), tfPostOnly), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(bob, 1));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // alice rests a 100-XRP AON offer (gives 100 XRP, wants 100 USD).
        env(offer(alice, usd(100), XRP(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1));

        // bob only wants 50 XRP — less than alice's all-or-none size, but at a
        // clearly-marketable price (60 USD). He must not partially consume her
        // offer; both rest untouched.
        env(offer(bob, XRP(50), usd(60)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(bob, 1));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, carol, bob);
        env.close();
        env.trust(usd(10'000), alice, carol, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, carol, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // alice's AON sits at the best price (100 XRP for 100 USD = 1.0).
        env(offer(alice, usd(100), XRP(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        // carol rests a worse-priced divisible offer (50 XRP for 55 USD = 1.1).
        env(offer(carol, usd(55), XRP(50)), Ter(tesSUCCESS));
        env.close();

        auto const aliceBefore = env.balance(alice, usd);
        // bob wants 50 XRP and will pay up to 120 USD. He cannot take alice's
        // 100-XRP AON in full, so he trades through it and takes carol's 50.
        env(offer(bob, XRP(50), usd(120)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(carol, 0), Offers(bob, 0));
        // alice's AON was untouched (no funds moved).
        BEAST_EXPECT(env.balance(alice, usd) == aliceBefore);
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
        auto const usd = gw["USD"];

        {
            // The field is not allowed without the amendment.
            Env env{*this, testableAmendments() - featureOfferQualifiers};
            env.fund(XRP(10'000), gw, alice);
            env.close();
            env(offerWithMinQty(alice, usd(100), XRP(100), XRP(50)), Ter(temDISABLED));
        }

        Env env{*this, testableAmendments()};
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(10'000), alice);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // AllOrNone is MinQuantity pinned to the full size; both is malformed.
        {
            auto jv = offerWithMinQty(alice, usd(100), XRP(100), XRP(50));
            jv[jss::Flags] = tfAllOrNone;
            env(jv, Ter(temMALFORMED));
        }
        // The floor must be denominated in the TakerGets asset.
        env(offerWithMinQty(alice, usd(100), XRP(100), usd(50)), Ter(temMALFORMED));
        // The floor must be positive and no larger than TakerGets.
        env(offerWithMinQty(alice, usd(100), XRP(100), XRP(0)), Ter(temMALFORMED));
        env(offerWithMinQty(alice, usd(100), XRP(100), XRP(101)), Ter(temMALFORMED));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // bob's resting offer can absorb only 30 XRP of alice's 100.
        env(offer(bob, XRP(30), usd(30)), Ter(tesSUCCESS));
        env.close();

        auto const aliceUsdBefore = env.balance(alice, usd);
        auto const seq = env.seq(alice);
        // alice gives 100 XRP with a 50-XRP floor: only 30 is immediately
        // obtainable, so nothing executes and the whole offer rests.
        env(offerWithMinQty(alice, usd(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        env.require(Offers(alice, 1), Offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, usd) == aliceUsdBefore);
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // bob's resting offer absorbs 60 XRP — above alice's 50-XRP floor.
        env(offer(bob, XRP(60), usd(60)), Ter(tesSUCCESS));
        env.close();

        auto const seq = env.seq(alice);
        env(offerWithMinQty(alice, usd(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        // 60 crossed; the 40-XRP remainder rests, still carrying the floor.
        env.require(Offers(alice, 1), Offers(bob, 0));
        BEAST_EXPECT(env.balance(alice, usd) == usd(1'060));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // alice rests 100 XRP with a 50-XRP floor.
        env(offerWithMinQty(alice, usd(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        // bob wants only 30 XRP at a marketable price: below the floor, so
        // alice's offer is skipped and bob's offer rests.
        env(offer(bob, XRP(30), usd(36)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, usd) == usd(1'000));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob, carol, dan);
        env.close();
        env.trust(usd(10'000), alice, bob, carol, dan);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, bob, usd(1'000)));
        env(pay(gw, carol, usd(1'000)));
        env(pay(gw, dan, usd(1'000)));
        env.close();

        auto const seq = env.seq(alice);
        env(offerWithMinQty(alice, usd(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();

        // bob takes exactly the floor: fills 50, remainder 50 rests.
        env(offer(bob, XRP(50), usd(50)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(bob, 0));
        BEAST_EXPECT(env.balance(alice, usd) == usd(1'050));
        auto const sle = env.le(keylet::offer(alice.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
        {
            BEAST_EXPECT(sle->getFieldAmount(sfTakerGets) == XRP(50));
            BEAST_EXPECT(sle->getFieldAmount(sfMinQuantity) == XRP(50));
        }

        // carol wants 30 — below the remainder's floor (min(50, 50)): skipped.
        env(offer(carol, XRP(30), usd(36)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(carol, 1));
        BEAST_EXPECT(env.balance(alice, usd) == usd(1'050));

        // dan takes the whole 50-XRP remainder: the offer is consumed. (His
        // demand also sweeps carol's resting 30-XRP bid first, so he asks for
        // 80 in total.)
        env(offer(dan, XRP(80), usd(96)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 0));
        BEAST_EXPECT(env.balance(alice, usd) == usd(1'100));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        // alice holds only 50 USD but posts a 100-USD all-or-none offer:
        // partially funded, so it survives the zero-funded reap but can never
        // satisfy its own all-or-none size.
        env(pay(gw, alice, usd(50)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1));

        // bob's demand (100 USD) covers alice's full size, but she cannot
        // deliver it. Her offer is reaped; bob crosses nothing and rests.
        env(offer(bob, usd(100), XRP(100)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 0), Offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, usd) == usd(50));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        // alice holds 30 USD, below her offer's 50-USD floor.
        env(pay(gw, alice, usd(30)));
        env(pay(gw, bob, usd(1'000)));
        env.close();

        env(offerWithMinQty(alice, XRP(100), usd(100), usd(50)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1));

        // alice can fund only 30 USD — below her own floor — so no fill can
        // satisfy it. The crosser reaps it.
        env(offer(bob, usd(100), XRP(100)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 0), Offers(bob, 1));
        BEAST_EXPECT(env.balance(alice, usd) == usd(30));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, carol);
        env.close();
        env.trust(usd(10'000), alice, carol);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, carol, usd(100)));
        env.close();

        // carol spoofs the book: she rests a better-priced offer giving USD,
        // then moves the USD away, leaving the offer unfunded.
        env(offer(carol, XRP(50), usd(100)), Ter(tesSUCCESS));
        env.close();
        env(pay(carol, gw, usd(100)));
        env.close();

        // alice's post-only would cross carol's price — but only against
        // funded liquidity. The spoof is inert; alice's offer rests.
        env(offer(alice, usd(100), XRP(100), tfPostOnly), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1));
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
        auto const usd = gw["USD"];

        auto const setup = [&](Env& env) {
            env.fund(XRP(10'000), gw, alice, bob, carol);
            env.close();
            env.trust(usd(10'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, usd(1'000)));
            env(pay(gw, bob, usd(1'000)));
            env.close();
        };

        {
            // Control: a plain resting offer routes bob's cross-currency
            // payment (bob pays USD, carol receives XRP via alice's offer).
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(alice, usd(100), XRP(100)), Ter(tesSUCCESS));
            env.close();
            env(pay(bob, carol, XRP(50)), Sendmax(usd(60)), Ter(tesSUCCESS));
            env.close();
        }
        {
            // The same book with an AON offer: a payment strand never
            // consumes contingent offers, even when the size would fit.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(alice, usd(100), XRP(100), tfAllOrNone), Ter(tesSUCCESS));
            env.close();
            env(pay(bob, carol, XRP(50)), Sendmax(usd(60)), Ter(tecPATH_PARTIAL));
            env.close();
            env.require(Offers(alice, 1));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(10'000), alice);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // With no liquidity a sell-AON rests whole, carrying both flags.
        auto const seq = env.seq(alice);
        env(offer(alice, XRP(100), usd(100), tfAllOrNone | tfSell), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1));
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
        auto const usd = gw["USD"];
        env.fund(XRP(1'000'000), gw, alice, bob);
        env.close();
        env.trust(usd(1'000'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(100'000)));
        env(pay(gw, bob, usd(100'000)));
        env.close();

        // A deep pool: 100k XRP / 100k USD, mid price 1.0.
        AMM const amm(env, alice, XRP(100'000), usd(100'000));

        // bob's small AON is fully satisfiable from the pool: it crosses in
        // full and nothing rests.
        env(offer(bob, XRP(100), usd(102), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(bob, 0));

        // bob's over-priced AON cannot fully cross within its limit quality:
        // it executes nothing and rests whole.
        auto const seq = env.seq(bob);
        env(offer(bob, XRP(50'000), usd(50'000), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(bob, 1));
        auto const sle = env.le(keylet::offer(bob.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
            BEAST_EXPECT(sle->getFieldAmount(sfTakerGets) == usd(50'000));
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
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const setup = [&](Env& env) {
            env.fund(XRP(100'000), gw, alice, bob);
            env.close();
            env.trust(usd(100'000), alice, bob);
            env.trust(eur(100'000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, alice, eur(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();
            // Bridge legs, both 1:1: USD→XRP and XRP→EUR. The XRP→EUR leg
            // carries a 50-EUR minimum. No direct USD/EUR book, so bob can
            // only fill by autobridging through XRP.
            env(offer(alice, usd(100), XRP(100)), Ter(tesSUCCESS));
            env(offerWithMinQty(alice, XRP(100), eur(100), eur(50)), Ter(tesSUCCESS));
            env.close();
        };

        {
            // bob wants 100 EUR — above the 50 floor — so the bridge fully
            // engages and the MinQty leg is consumed in full.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(bob, eur(100), usd(100)), Ter(tesSUCCESS));
            env.close();
            env.require(Offers(bob, 0));
            BEAST_EXPECT(env.balance(bob, eur) == eur(100));
        }
        {
            // bob wants only 30 EUR — below the floor — so the MinQty leg is
            // skipped, the bridge cannot deliver, and bob's offer rests
            // untouched. alice's legs remain; no sub-floor fill occurs.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(bob, eur(30), usd(30)), Ter(tesSUCCESS));
            env.close();
            env.require(Offers(bob, 1), Offers(alice, 2));
            BEAST_EXPECT(env.balance(bob, eur) == eur(0));
        }
        {
            // bob wants exactly 50 EUR — the floor boundary — the leg is
            // consumed to exactly its floor and the remainder rests.
            Env env{*this, testableAmendments()};
            setup(env);
            env(offer(bob, eur(50), usd(50)), Ter(tesSUCCESS));
            env.close();
            env.require(Offers(bob, 0));
            BEAST_EXPECT(env.balance(bob, eur) == eur(50));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, carol);
        env.close();
        env.trust(usd(10'000), alice, carol);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env(pay(gw, carol, usd(1'000)));
        env.close();

        // Both give USD and want XRP: same book, distinct qualifiers.
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        env(offerWithMinQty(carol, XRP(100), usd(100), usd(40)), Ter(tesSUCCESS));
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

    void
    testAccountOffersMarkers()
    {
        testcase("account_offers marks contingent entries");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(10'000), alice);
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        auto const aonSeq = env.seq(alice);
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        auto const minQtySeq = env.seq(alice);
        env(offerWithMinQty(alice, XRP(100), usd(100), usd(40)), Ter(tesSUCCESS));
        auto const plainSeq = env.seq(alice);
        env(offer(alice, XRP(100), usd(100)), Ter(tesSUCCESS));
        env.close();

        auto const result = env.rpc("account_offers", alice.human())[jss::result];
        if (!BEAST_EXPECT(result.isMember(jss::offers) && result[jss::offers].size() == 3))
            return;
        for (auto const& jvOffer : result[jss::offers])
        {
            auto const seq = jvOffer[jss::seq].asUInt();
            BEAST_EXPECT(jvOffer.isMember(jss::all_or_none) == (seq == aonSeq));
            BEAST_EXPECT(jvOffer.isMember(jss::min_quantity) == (seq == minQtySeq));
            if (seq == aonSeq)
                BEAST_EXPECT(jvOffer[jss::all_or_none].asBool());
            if (seq == minQtySeq)
            {
                BEAST_EXPECT(
                    jvOffer[jss::min_quantity] ==
                    usd(40).value().getJson(JsonOptions::Values::None));
            }
            BEAST_EXPECT(seq == aonSeq || seq == minQtySeq || seq == plainSeq);
        }
    }

    // ---- Group K: contingent offers beside other offers at their quality ----

    // gw issues USD to alice, bob and carol; alice is funded with aliceUsd.
    static void
    fundSameQuality(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& alice,
        jtx::Account const& bob,
        jtx::Account const& carol,
        jtx::IOU const& usd,
        double aliceUsd)
    {
        using namespace jtx;
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        env.trust(usd(10'000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, usd(aliceUsd)));
        env(pay(gw, bob, usd(1'000)));
        env(pay(gw, carol, usd(1'000)));
        env.close();
    }

    void
    testKeptAonAheadOfSameQualityOffer()
    {
        testcase("kept AON ahead of an offer at its quality");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];
        fundSameQuality(env, gw, alice, bob, carol, usd, 1'000);

        // alice's AON heads the 1.0 directory; bob's offer follows it there.
        auto const aonSeq = env.seq(alice);
        env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
        env.close();
        env(offer(bob, XRP(50), usd(50)), Ter(tesSUCCESS));
        env.close();

        // carol wants 50 USD: alice's AON is kept and the walk reaches bob in
        // the same directory.
        env(offer(carol, usd(50), XRP(50)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(alice, 1), Offers(bob, 0), Offers(carol, 0));
        BEAST_EXPECT(offerFlags(env, alice, aonSeq) & lsfAllOrNone);
        BEAST_EXPECT(env.balance(alice, usd) == usd(1'000));
        BEAST_EXPECT(env.balance(bob, usd) == usd(950));
        BEAST_EXPECT(env.balance(carol, usd) == usd(1'050));
    }

    void
    testRestingAonTakenInFull()
    {
        testcase("resting AON is taken in full by a large enough taker");
        using namespace jtx;
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];

        // carol wants exactly alice's 100 USD, or 150 USD with 50 left to rest.
        for (int const want : {100, 150})
        {
            Env env{*this, testableAmendments()};
            fundSameQuality(env, gw, alice, bob, carol, usd, 1'000);

            env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
            env.close();

            env(offer(carol, usd(want), XRP(want)), Ter(tesSUCCESS));
            env.close();
            env.require(Offers(alice, 0), Offers(carol, want == 100 ? 0 : 1));
            BEAST_EXPECT(env.balance(alice, usd) == usd(900));
            BEAST_EXPECT(env.balance(carol, usd) == usd(1'100));
        }
    }

    void
    testAonAfterAttemptedSameQualityOffer()
    {
        testcase("AON behind a taken offer at its quality");
        using namespace jtx;
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];

        // bob's offer heads the 1.0 directory and alice's AON follows it.
        // carol wants 80 USD: bob's 50 are taken first, then alice's AON is
        // kept when funded (30 left is short of 100) or reaped when not.
        for (bool const funded : {true, false})
        {
            Env env{*this, testableAmendments()};
            fundSameQuality(env, gw, alice, bob, carol, usd, funded ? 1'000 : 60);

            env(offer(bob, XRP(50), usd(50)), Ter(tesSUCCESS));
            env.close();
            env(offer(alice, XRP(100), usd(100), tfAllOrNone), Ter(tesSUCCESS));
            env.close();

            env(offer(carol, usd(80), XRP(80)), Ter(tesSUCCESS));
            env.close();
            env.require(Offers(alice, funded ? 1 : 0), Offers(bob, 0), Offers(carol, 1));
            BEAST_EXPECT(env.balance(alice, usd) == usd(funded ? 1'000 : 60));
            BEAST_EXPECT(env.balance(carol, usd) == usd(1'050));
        }
    }

    void
    testMinQtyEntryFullyCrosses()
    {
        testcase("MinQty entry that crosses in full places nothing");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];
        fundSameQuality(env, gw, alice, bob, carol, usd, 1'000);

        env(offer(bob, XRP(100), usd(100)), Ter(tesSUCCESS));
        env.close();

        env(offerWithMinQty(carol, usd(100), XRP(100), XRP(50)), Ter(tesSUCCESS));
        env.close();
        env.require(Offers(bob, 0), Offers(carol, 0));
        BEAST_EXPECT(env.balance(carol, usd) == usd(1'100));
        BEAST_EXPECT(env.balance(bob, usd) == usd(900));
    }

    void
    testMinQtySellRemainderRoundsToZero()
    {
        testcase("MinQty sell whose remainder rounds to no output places nothing");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];
        fundSameQuality(env, gw, alice, bob, carol, usd, 1'000);

        // bob pays 10 drops for 9.6 USD.
        env(offer(bob, usd(9.6), drops(10)), Ter(tesSUCCESS));
        env.close();

        // carol sells 10 USD for at least 10 drops. bob takes 9.6 USD; the
        // 0.4 USD left would ask for 0.4 drops, which rounds to none.
        auto jv = offerWithMinQty(carol, drops(10), usd(10), usd(1));
        jv[jss::Flags] = tfSell;
        env(jv, Ter(tesSUCCESS));
        env.close();
        env.require(Offers(bob, 0), Offers(carol, 0));
        BEAST_EXPECT(env.balance(carol, usd) == usd(990.4));
        BEAST_EXPECT(env.balance(bob, usd) == usd(1'009.6));
    }

    void
    testMinQtyTakerUnfundedAfterFee()
    {
        testcase("MinQty taker left unfunded by the fee does not cross");
        using namespace jtx;
        Env env{*this, testableAmendments()};
        auto const gw = Account{"gw"};
        auto const dan = Account{"dan"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw);
        env.close();

        // dan holds the reserve plus one fee: preclaim sees XRP to sell, and
        // once the fee is charged none is left.
        auto const fee = env.current()->fees().base;
        env.fund(env.current()->fees().accountReserve(0, 1) + fee, dan);
        env.close();
        env(offer(gw, XRP(1), usd(1)), Ter(tesSUCCESS));
        env.close();

        env(offerWithMinQty(dan, usd(1'000), XRP(1'000), XRP(1)), Ter(tecUNFUNDED_OFFER));
        env.close();
        env.require(Offers(dan, 0), Offers(gw, 1));
    }

    // ---- Group J: MPT input limits ----

    // Expect the offer at (acct, seq) to rest with exactly these amounts.
    void
    expectOffer(
        jtx::Env& env,
        jtx::Account const& acct,
        std::uint32_t seq,
        STAmount const& pays,
        STAmount const& gets)
    {
        auto const sle = env.le(keylet::offer(acct.id(), SeqProxy::rawSequence(seq)));
        if (BEAST_EXPECT(sle))
        {
            BEAST_EXPECT((*sle)[sfTakerPays] == pays);
            BEAST_EXPECT((*sle)[sfTakerGets] == gets);
        }
    }

    void
    testRestingContingentAboveMaximumAmount()
    {
        // BookStep limits an MPT input to the MPT's MaximumAmount, so a
        // crossing takes at most the cap from a resting bid above it.
        testcase("resting contingent bid above MaximumAmount");
        using namespace jtx;

        Account const gwX("gwX");
        Account const gwY("gwY");
        Account const alice("alice");
        Account const mallory("mallory");
        std::int64_t constexpr cap = 1'000;

        // mallory bids x(2 * cap) for y(2 * cap); alice sells the whole x
        // supply at the same price.
        auto const test = [&](std::optional<std::int64_t> minQty, bool keptWhole) {
            Env env{*this, testableAmendments()};
            env.fund(XRP(10'000), gwX, gwY, alice, mallory);
            env.close();
            MPT const x =
                MPTTester({.env = env, .issuer = gwX, .holders = {alice, mallory}, .maxAmt = cap});
            MPT const y = MPTTester({.env = env, .issuer = gwY, .holders = {alice, mallory}});
            env(pay(gwX, alice, x(cap)));
            env(pay(gwY, mallory, y(2 * cap)));
            env.close();

            auto const bidSeq = env.seq(mallory);
            if (minQty)
            {
                env(offerWithMinQty(mallory, x(2 * cap), y(2 * cap), y(*minQty)), Ter(tesSUCCESS));
            }
            else
            {
                env(offer(mallory, x(2 * cap), y(2 * cap), tfAllOrNone), Ter(tesSUCCESS));
            }
            env.close();

            env(offer(alice, y(cap), x(cap)), Ter(tesSUCCESS));
            env.close();

            if (keptWhole)
            {
                expectOffer(env, mallory, bidSeq, x(2 * cap), y(2 * cap));
                BEAST_EXPECT(env.balance(alice, x) == x(cap));
                BEAST_EXPECT(env.balance(mallory, y) == y(2 * cap));
                env.require(Offers(alice, 1));
            }
            else
            {
                expectOffer(env, mallory, bidSeq, x(cap), y(cap));
                BEAST_EXPECT(env.balance(alice, x) == x(0));
                BEAST_EXPECT(env.balance(alice, y) == y(cap));
                env.require(Offers(alice, 0));
            }
        };

        // All-or-none: the cap lets half the bid through, so it is kept whole.
        test(std::nullopt, true);
        // A floor above what the cap lets through: kept whole, not reaped.
        test(1'500, true);
        // A floor within what the cap lets through: half the bid fills.
        test(800, false);
    }

    void
    testRestingContingentIssuerTaker()
    {
        // When the taker issues the book's input MPT, BookStep is the first
        // step and limits the input to what the issuer can still issue.
        testcase("resting contingent bid crossed by the input's issuer");
        using namespace jtx;

        Account const gwX("gwX");
        Account const gwY("gwY");
        Account const carol("carol");
        Account const mallory("mallory");
        std::int64_t constexpr cap = 1'000;
        std::int64_t constexpr outstanding = 600;
        std::int64_t constexpr available = cap - outstanding;

        // mallory bids x(cap) for y(cap); gwX, able to issue only 400 more x,
        // sells x(cap) at the same price.
        auto const test = [&](std::optional<std::int64_t> minQty, bool keptWhole) {
            Env env{*this, testableAmendments()};
            env.fund(XRP(10'000), gwX, gwY, carol, mallory);
            env.close();
            MPT const x =
                MPTTester({.env = env, .issuer = gwX, .holders = {carol, mallory}, .maxAmt = cap});
            MPT const y = MPTTester({.env = env, .issuer = gwY, .holders = {gwX, mallory}});
            env(pay(gwX, carol, x(outstanding)));
            env(pay(gwY, mallory, y(cap)));
            env.close();

            auto const bidSeq = env.seq(mallory);
            if (minQty)
            {
                env(offerWithMinQty(mallory, x(cap), y(cap), y(*minQty)), Ter(tesSUCCESS));
            }
            else
            {
                env(offer(mallory, x(cap), y(cap), tfAllOrNone), Ter(tesSUCCESS));
            }
            env.close();

            env(offer(gwX, y(cap), x(cap)), Ter(tesSUCCESS));
            env.close();

            std::int64_t const taken = keptWhole ? 0 : available;
            if (keptWhole)
            {
                expectOffer(env, mallory, bidSeq, x(cap), y(cap));
            }
            else
            {
                expectOffer(env, mallory, bidSeq, x(cap - available), y(cap - available));
            }
            BEAST_EXPECT(env.balance(mallory, x) == x(taken));
            BEAST_EXPECT(env.balance(mallory, y) == y(cap - taken));
            BEAST_EXPECT(env.balance(gwX, y) == y(taken));
            auto const issuance = env.le(keylet::mptokenIssuance(x.mpt()));
            if (BEAST_EXPECT(issuance))
            {
                BEAST_EXPECT(
                    (*issuance)[sfOutstandingAmount] ==
                    static_cast<std::uint64_t>(outstanding + taken));
            }
        };

        // All-or-none: only 400 of the bid's 1,000 can be issued, so it is
        // kept whole.
        test(std::nullopt, true);
        // A floor above what can be issued: kept whole, not reaped.
        test(500, true);
        // A floor within what can be issued: 400 of the bid fills.
        test(300, false);
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
        testRestingContingentAboveMaximumAmount();
        testRestingContingentIssuerTaker();
        testAccountOffersMarkers();
        testKeptAonAheadOfSameQualityOffer();
        testRestingAonTakenInFull();
        testAonAfterAttemptedSameQualityOffer();
        testMinQtyEntryFullyCrosses();
        testMinQtySellRemainderRoundsToZero();
        testMinQtyTakerUnfundedAfterFee();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(OfferQualifiers, app, xrpl, 2);

}  // namespace xrpl::test
