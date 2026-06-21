#include <test/formal_verification/ffi/ledger/ViewFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/deposit.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>

#include <cstdint>
#include <optional>

namespace xrpl::test {

using namespace formal_verification;

class LeanCanWithdraw_test : public LedgerSuite
{
    void
    runCanWithdrawToSle(
        ReadView const& view,
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        bool hasDestinationTag,
        TER expected,
        char const* label)
    {
        auto const toSle = view.read(keylet::account(to));
        std::optional<AccountRootFFI> toSleFFI;
        if (toSle)
            toSleFFI = AccountRootFFIBuilder()
                           .fromCpp(ledger_entries::AccountRoot(toSle))
                           .build(toSle->key());
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = canWithdraw(view, from, to, toSle, amount, hasDestinationTag);
            LeanTerResult const leanRes = formal_verification::canWithdrawToSle(
                ledger, from, to, toSleFFI ? &*toSleFFI : nullptr, amount, hasDestinationTag);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    runCanWithdrawFromTo(
        ReadView const& view,
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        bool hasDestinationTag,
        TER expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = canWithdraw(view, from, to, amount, hasDestinationTag);
            LeanTerResult const leanRes =
                formal_verification::canWithdrawFromTo(ledger, from, to, amount, hasDestinationTag);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    runCanWithdrawTx(
        ReadView const& view,
        AccountID const& from,
        std::optional<AccountID> const& to,
        STAmount const& amount,
        std::optional<uint32_t> const& destinationTag,
        TER expected,
        char const* label)
    {
        STTx const tx(ttVAULT_WITHDRAW, [&](STObject& obj) {
            obj.setAccountID(sfAccount, from);
            if (to)
                obj.setAccountID(sfDestination, *to);
            obj.setFieldAmount(sfAmount, amount);
            if (destinationTag)
                obj.setFieldU32(sfDestinationTag, *destinationTag);
        });
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = canWithdraw(view, tx);
            LeanTerResult const leanRes =
                formal_verification::canWithdrawTx(ledger, from, to, amount, destinationTag);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testCanWithdrawToSle()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");  // withdrawer (from)
        Account const bob("bob");      // plain destination, trusts gw
        Account const carol("carol");  // requires destination tag
        Account const dave("dave");    // deposit auth, preauthorizes alice
        Account const ed("ed");        // deposit auth, no preauth
        Account const fred("fred");    // no USD line
        Account const ghost("ghost");  // never created
        Env env(*this);
        env.fund(XRP(1000), gw, alice, bob, carol, dave, ed, fred);
        env(fset(carol, asfRequireDest));
        env(fset(dave, asfDepositAuth));
        env(fset(ed, asfDepositAuth));
        env(deposit::auth(dave, alice));
        env(trust(bob, gw["USD"](1000)));
        env.close();
        auto const& view = *env.current();
        STAmount const xrp = XRP(10);

        runCanWithdrawToSle(
            view, alice.id(), ghost.id(), xrp, false, tecNO_DST, "canWithdrawToSle.no_dst");
        runCanWithdrawToSle(
            view,
            alice.id(),
            carol.id(),
            xrp,
            false,
            tecDST_TAG_NEEDED,
            "canWithdrawToSle.tag_needed");
        runCanWithdrawToSle(
            view, alice.id(), carol.id(), xrp, true, tesSUCCESS, "canWithdrawToSle.tag_provided");
        runCanWithdrawToSle(
            view, alice.id(), alice.id(), xrp, false, tesSUCCESS, "canWithdrawToSle.self");
        runCanWithdrawToSle(
            view, alice.id(), ed.id(), xrp, false, tecNO_PERMISSION, "canWithdrawToSle.no_preauth");
        runCanWithdrawToSle(
            view, alice.id(), dave.id(), xrp, false, tesSUCCESS, "canWithdrawToSle.preauth");
        // withdrawToDestExceedsLimit
        runCanWithdrawToSle(
            view, alice.id(), bob.id(), xrp, false, tesSUCCESS, "canWithdrawToSle.xrp");
        runCanWithdrawToSle(
            view,
            alice.id(),
            fred.id(),
            gw["USD"](100),
            false,
            tecNO_LINE,
            "canWithdrawToSle.iou_exceeds");
        runCanWithdrawToSle(
            view,
            alice.id(),
            bob.id(),
            gw["USD"](10),
            false,
            tesSUCCESS,
            "canWithdrawToSle.iou_ok");
        runCanWithdrawToSle(
            view,
            alice.id(),
            bob.id(),
            STAmount{MPTAmount{10}, MPTIssue{makeMptID(1, gw.id())}},
            false,
            tesSUCCESS,
            "canWithdrawToSle.mpt");
    }

    void
    testCanWithdrawFromTo()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const ghost("ghost");
        Env env(*this);
        env.fund(XRP(1000), alice, bob);
        env.close();
        auto const& view = *env.current();
        STAmount const xrp = XRP(10);

        runCanWithdrawFromTo(
            view, alice.id(), bob.id(), xrp, false, tesSUCCESS, "canWithdrawFromTo.present");
        runCanWithdrawFromTo(
            view, alice.id(), ghost.id(), xrp, false, tecNO_DST, "canWithdrawFromTo.absent");
    }

    void
    testCanWithdrawTx()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");  // requires destination tag
        Env env(*this);
        env.fund(XRP(1000), alice, bob, carol);
        env(fset(carol, asfRequireDest));
        env.close();
        auto const& view = *env.current();
        STAmount const xrp = XRP(10);

        runCanWithdrawTx(
            view, alice.id(), bob.id(), xrp, std::nullopt, tesSUCCESS, "canWithdrawTx.dest");
        runCanWithdrawTx(
            view, alice.id(), carol.id(), xrp, uint32_t{5}, tesSUCCESS, "canWithdrawTx.tag");
        runCanWithdrawTx(
            view, alice.id(), std::nullopt, xrp, std::nullopt, tesSUCCESS, "canWithdrawTx.no_dest");
    }

    void
    runTests() override
    {
        testCanWithdrawToSle();
        testCanWithdrawFromTo();
        testCanWithdrawTx();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCanWithdraw, formal_verification, xrpl);

}  // namespace xrpl::test
