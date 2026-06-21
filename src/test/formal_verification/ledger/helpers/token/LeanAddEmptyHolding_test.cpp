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

class LeanAddEmptyHolding_test : public LedgerSuite
{
    void
    runAddEmptyHolding(
        Sandbox& sb,
        AccountID const& account,
        XRPAmount priorBalance,
        Asset const& asset,
        TER expected,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = addEmptyHolding(
                sb, account, priorBalance, asset, beast::Journal{beast::Journal::getNullSink()});
            LeanTerResult const leanRes =
                formal_verification::addEmptyHolding(ledger, account, priorBalance, asset);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testAddEmptyHoldingXRP()
    {
        using namespace jtx;
        Account const alice("alice");
        Env env(*this);
        env.fund(XRP(1000), alice);
        env.close();
        XRPAmount const reserve = env.current()->fees().accountReserve(1);
        Sandbox sb(&*env.current(), TapNone);
        runAddEmptyHolding(
            sb, alice.id(), reserve, Asset{xrpIssue()}, tesSUCCESS, "addEmptyHolding.xrp");
    }

    void
    testAddEmptyHoldingIOU()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const gwNoRipple("gwNoRipple");  // issuer without DefaultRipple
        Account const gwFrozen("gwFrozen");      // globally frozen issuer
        Account const alice("alice");            // already holds a USD line
        Account const bob("bob");                // no line yet
        Account const ghost("ghost");            // never funded
        Env env(*this);
        env.fund(XRP(1000), gw, alice, bob, gwFrozen);
        env.fund(XRP(1000), noripple(gwNoRipple));
        env(fset(gw, asfDefaultRipple));
        env(fset(gwFrozen, asfGlobalFreeze));
        env(trust(alice, gw["USD"](100)));
        env.close();
        Asset const usd{gw["USD"].issue()};
        XRPAmount const reserve = env.current()->fees().accountReserve(2);

        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(sb, gw.id(), reserve, usd, tesSUCCESS, "addEmptyHolding.iou_self");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb,
                alice.id(),
                reserve,
                Asset{gwFrozen["USD"].issue()},
                tecFROZEN,
                "addEmptyHolding.iou_frozen");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb, ghost.id(), reserve, usd, tefINTERNAL, "addEmptyHolding.iou_account_absent");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb,
                alice.id(),
                reserve,
                Asset{ghost["USD"].issue()},
                tefINTERNAL,
                "addEmptyHolding.iou_issuer_absent");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb,
                alice.id(),
                reserve,
                Asset{gwNoRipple["USD"].issue()},
                tecINTERNAL,
                "addEmptyHolding.iou_no_default_ripple");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb, alice.id(), reserve, usd, tecDUPLICATE, "addEmptyHolding.iou_duplicate");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb,
                bob.id(),
                XRPAmount{0},
                usd,
                tecNO_LINE_INSUF_RESERVE,
                "addEmptyHolding.iou_insufficient_reserve");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb, bob.id(), reserve, usd, tesSUCCESS, "addEmptyHolding.iou_success");
        }
    }

    void
    testAddEmptyHoldingMPT()
    {
        using namespace jtx;
        {
            Account const bob("bob");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{.holders = {bob}, .create = MPTCreate{.flags = tfMPTCanTransfer}});
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            XRPAmount const reserve = env.current()->fees().accountReserve(2);
            {
                Sandbox sb(&*env.current(), TapNone);
                MPTIssue const absent{makeMptID(99, mpt.issuer().id())};
                runAddEmptyHolding(
                    sb,
                    bob.id(),
                    reserve,
                    Asset{absent},
                    tefINTERNAL,
                    "addEmptyHolding.mpt_issuance_absent");
            }
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mpt.issuanceID()));
                iss->setFieldU32(sfFlags, iss->getFieldU32(sfFlags) | lsfMPTLocked);
                sb.update(iss);
                runAddEmptyHolding(
                    sb, bob.id(), reserve, mptAsset, tefINTERNAL, "addEmptyHolding.mpt_locked");
            }
            {
                Sandbox sb(&*env.current(), TapNone);
                runAddEmptyHolding(
                    sb,
                    mpt.issuer().id(),
                    reserve,
                    mptAsset,
                    tesSUCCESS,
                    "addEmptyHolding.mpt_self");
            }
            {
                Sandbox sb(&*env.current(), TapNone);
                runAddEmptyHolding(
                    sb, bob.id(), reserve, mptAsset, tesSUCCESS, "addEmptyHolding.mpt_success");
            }
        }
        {
            Account const bob("bob");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            XRPAmount const reserve = env.current()->fees().accountReserve(2);
            Sandbox sb(&*env.current(), TapNone);
            runAddEmptyHolding(
                sb, bob.id(), reserve, mptAsset, tecDUPLICATE, "addEmptyHolding.mpt_duplicate");
        }
    }

    void
    runTests() override
    {
        testAddEmptyHoldingXRP();
        testAddEmptyHoldingIOU();
        testAddEmptyHoldingMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAddEmptyHolding, formal_verification, xrpl);

}  // namespace xrpl::test
