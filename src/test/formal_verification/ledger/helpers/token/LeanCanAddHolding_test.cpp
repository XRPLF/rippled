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

class LeanCanAddHolding_test : public LedgerSuite
{
    void
    runCanAddHolding(jtx::Env& env, Asset const& asset, TER expected, char const* label)
    {
        auto const& view = *env.current();
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = canAddHolding(view, asset);
            LeanTerResult const leanRes = formal_verification::canAddHolding(ledger, asset);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testCanAddHoldingXRP()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(1000), "alice");
        env.close();
        runCanAddHolding(env, Asset{xrpIssue()}, tesSUCCESS, "canAddHolding.xrp");
    }

    void
    testCanAddHoldingIOU()
    {
        using namespace jtx;
        {
            Env env(*this);
            Account const gw("gw");  // never created
            env.fund(XRP(1000), "alice");
            env.close();
            runCanAddHolding(
                env, Asset{gw["USD"].issue()}, terNO_ACCOUNT, "canAddHolding.iou_issuer_absent");
        }
        {
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), noripple(gw));
            env.close();
            runCanAddHolding(
                env, Asset{gw["USD"].issue()}, terNO_RIPPLE, "canAddHolding.iou_no_default_ripple");
        }
        {
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), gw);
            env(fset(gw, asfDefaultRipple));
            env.close();
            runCanAddHolding(
                env, Asset{gw["USD"].issue()}, tesSUCCESS, "canAddHolding.iou_default_ripple");
        }
    }

    void
    testCanAddHoldingMPT()
    {
        using namespace jtx;
        {
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), gw);
            env.close();
            MPTIssue const absent{makeMptID(1, gw.id())};  // never created
            runCanAddHolding(
                env, Asset{absent}, tecOBJECT_NOT_FOUND, "canAddHolding.mpt_issuance_absent");
        }
        {
            Env env(*this);
            Account const gw("gw");
            MPTTester mpt(env, gw, MPTInit{.fund = true, .create = MPTCreate{.flags = 0}});
            runCanAddHolding(
                env,
                Asset{MPTIssue{mpt.issuanceID()}},
                tecNO_AUTH,
                "canAddHolding.mpt_no_can_transfer");
        }
        {
            Env env(*this);
            Account const gw("gw");
            MPTTester mpt(
                env, gw, MPTInit{.fund = true, .create = MPTCreate{.flags = tfMPTCanTransfer}});
            runCanAddHolding(
                env,
                Asset{MPTIssue{mpt.issuanceID()}},
                tesSUCCESS,
                "canAddHolding.mpt_can_transfer");
        }
    }

    void
    runTests() override
    {
        testCanAddHoldingXRP();
        testCanAddHoldingIOU();
        testCanAddHoldingMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCanAddHolding, formal_verification, xrpl);

}  // namespace xrpl::test
