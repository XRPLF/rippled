#include <test/formal_verification/ffi/ledger/helpers/TokenHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/credentials.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/rate.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <vector>

namespace xrpl::test {

using namespace formal_verification;

class LeanRemoveEmptyHolding_test : public LedgerSuite
{
    void
    runRemoveEmptyHolding(
        Sandbox& sb,
        AccountID const& account,
        Asset const& asset,
        TER expected,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = removeEmptyHolding(
                sb, account, asset, beast::Journal{beast::Journal::getNullSink()});
            LeanTerResult const leanRes =
                formal_verification::removeEmptyHolding(ledger, account, asset);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testRemoveEmptyHoldingXRP()
    {
        using namespace jtx;
        Account const alice("alice");  // funded, nonzero XRP
        Account const carol("carol");
        Account const ghost("ghost");  // never funded
        Env env(*this);
        env.fund(XRP(1000), alice, carol);
        env.close();

        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb,
                ghost.id(),
                Asset{xrpIssue()},
                tecINTERNAL,
                "removeEmptyHolding.xrp_account_absent");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, alice.id(), Asset{xrpIssue()}, tecHAS_OBLIGATIONS, "removeEmptyHolding.xrp");
        }
        {
            // a zero XRP balance reaches the success path
            Sandbox sb(&*env.current(), TapNone);
            auto acct = sb.peek(keylet::account(carol.id()));
            acct->setFieldAmount(sfBalance, STAmount{XRPAmount{0}});
            sb.update(acct);
            runRemoveEmptyHolding(
                sb, carol.id(), Asset{xrpIssue()}, tesSUCCESS, "removeEmptyHolding.xrp_empty");
        }
    }

    void
    testRemoveEmptyHoldingIOU()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");  // empty USD line
        Account const bob("bob");      // holds USD
        Account const carol("carol");  // no USD line
        Account const dave("dave");    // empty USD line
        Account const ed("ed");        // empty USD line
        Account const frank("frank");  // bilateral line with gw (both reserve flags set)
        Account const henry("henry");  // line with gw carrying only lsfHighReserve
        Env env(*this);
        env.fund(XRP(1000), gw, alice, bob, carol, dave, ed, frank, henry);
        env(fset(gw, asfDefaultRipple));
        env(trust(alice, gw["USD"](100)));
        env(trust(bob, gw["USD"](100)));
        env(trust(dave, gw["USD"](100)));
        env(trust(ed, gw["USD"](100)));
        env(trust(frank, gw["USD"](100)));  // frank's reserve on line(frank, gw)
        env(trust(gw, frank["USD"](100)));  // gw's reserve on the same line

        if (henry.id() > gw.id())
            env(trust(henry, gw["USD"](100)));
        else
            env(trust(gw, henry["USD"](100)));
        env(pay(gw, bob, gw["USD"](30)));
        env.close();
        Asset const usd{gw["USD"].issue()};
        AccountID const lowId = std::min(frank.id(), gw.id());
        AccountID const henryHighId = std::max(henry.id(), gw.id());

        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, gw.id(), usd, tesSUCCESS, "removeEmptyHolding.iou_issuer_no_line");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, carol.id(), usd, tecOBJECT_NOT_FOUND, "removeEmptyHolding.iou_no_line");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, bob.id(), usd, tecHAS_OBLIGATIONS, "removeEmptyHolding.iou_obligations");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            sb.erase(sb.peek(keylet::account(lowId)));
            runRemoveEmptyHolding(
                sb, frank.id(), usd, tecINTERNAL, "removeEmptyHolding.iou_low_account_absent");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            sb.erase(sb.peek(keylet::account(henryHighId)));
            runRemoveEmptyHolding(
                sb, henry.id(), usd, tecINTERNAL, "removeEmptyHolding.iou_high_account_absent");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, alice.id(), usd, tesSUCCESS, "removeEmptyHolding.iou_success");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, dave.id(), usd, tesSUCCESS, "removeEmptyHolding.iou_success_dave");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, ed.id(), usd, tesSUCCESS, "removeEmptyHolding.iou_success_ed");
        }
    }

    void
    testRemoveEmptyHoldingMPT()
    {
        using namespace jtx;
        Account const bob("bob");      // empty token
        Account const carol("carol");  // token with a balance
        Account const dan("dan");      // no token
        Env env(*this);
        MPTTester mpt(
            env,
            "gw",
            MPTInit{
                .holders = {bob, carol},
                .create = MPTCreate{
                    .authorize = std::make_optional(std::vector<Account>{}),
                    .flags = tfMPTCanTransfer}});
        env.fund(XRP(1000), dan);
        mpt.pay(mpt.issuer(), carol, 100);
        env.close();
        Asset const mptAsset{MPTIssue{mpt.issuanceID()}};

        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, dan.id(), mptAsset, tecOBJECT_NOT_FOUND, "removeEmptyHolding.mpt_no_token");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb,
                mpt.issuer().id(),
                mptAsset,
                tesSUCCESS,
                "removeEmptyHolding.mpt_issuer_no_token");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, carol.id(), mptAsset, tecHAS_OBLIGATIONS, "removeEmptyHolding.mpt_obligations");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runRemoveEmptyHolding(
                sb, bob.id(), mptAsset, tesSUCCESS, "removeEmptyHolding.mpt_success");
        }
    }

    void
    runTests() override
    {
        testRemoveEmptyHoldingXRP();
        testRemoveEmptyHoldingIOU();
        testRemoveEmptyHoldingMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanRemoveEmptyHolding, formal_verification, xrpl);

}  // namespace xrpl::test
