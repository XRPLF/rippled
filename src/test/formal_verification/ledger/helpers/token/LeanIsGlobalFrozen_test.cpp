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

class LeanIsGlobalFrozen_test : public LedgerSuite
{
    void
    runIsGlobalFrozen(ReadView const& view, Asset const& asset, bool expected, char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            bool const cpp = isGlobalFrozen(view, asset);
            LeanBoolResult const leanRes = formal_verification::isGlobalFrozen(ledger, asset);
            BEAST_EXPECT(cpp == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cpp);
        });
    }

    void
    testIsGlobalFrozenXRP()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(1000), "alice");
        env.close();
        runIsGlobalFrozen(*env.current(), Asset{xrpIssue()}, false, "isGlobalFrozen.xrp");
    }

    void
    testIsGlobalFrozenIOU()
    {
        using namespace jtx;
        {  // issuer without global freeze
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), gw);
            env.close();
            runIsGlobalFrozen(
                *env.current(), Asset{gw["USD"].issue()}, false, "isGlobalFrozen.iou_unfrozen");
        }
        {  // issuer with global freeze
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), gw);
            env(fset(gw, asfGlobalFreeze));
            env.close();
            runIsGlobalFrozen(
                *env.current(), Asset{gw["USD"].issue()}, true, "isGlobalFrozen.iou_global_freeze");
        }
        {  // issuer account never created
            Env env(*this);
            Account const gw("gw");  // never funded
            env.fund(XRP(1000), "alice");
            env.close();
            runIsGlobalFrozen(
                *env.current(),
                Asset{gw["USD"].issue()},
                false,
                "isGlobalFrozen.iou_issuer_absent");
        }
    }

    void
    testIsGlobalFrozenMPT()
    {
        using namespace jtx;
        {  // issuance never created
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), gw);
            env.close();
            MPTIssue const absent{makeMptID(1, gw.id())};
            runIsGlobalFrozen(
                *env.current(), Asset{absent}, false, "isGlobalFrozen.mpt_issuance_absent");
        }
        {  // issuance not locked
            Env env(*this);
            Account const gw("gw");
            MPTTester mpt(
                env, gw, MPTInit{.fund = true, .create = MPTCreate{.flags = tfMPTCanLock}});
            runIsGlobalFrozen(
                *env.current(),
                Asset{MPTIssue{mpt.issuanceID()}},
                false,
                "isGlobalFrozen.mpt_unlocked");
        }
        {  // issuance locked
            Env env(*this);
            Account const gw("gw");
            MPTTester mpt(
                env, gw, MPTInit{.fund = true, .create = MPTCreate{.flags = tfMPTCanLock}});
            mpt.set({.account = gw, .flags = tfMPTLock});
            runIsGlobalFrozen(
                *env.current(),
                Asset{MPTIssue{mpt.issuanceID()}},
                true,
                "isGlobalFrozen.mpt_locked");
        }
    }

    void
    runTests() override
    {
        testIsGlobalFrozenXRP();
        testIsGlobalFrozenIOU();
        testIsGlobalFrozenMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanIsGlobalFrozen, formal_verification, xrpl);

}  // namespace xrpl::test
