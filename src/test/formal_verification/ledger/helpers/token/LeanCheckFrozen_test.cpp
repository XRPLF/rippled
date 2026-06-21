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

class LeanCheckFrozen_test : public LedgerSuite
{
    void
    runCheckFrozen(
        ReadView const& view,
        AccountID const& account,
        Asset const& asset,
        TER expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = checkFrozen(view, account, asset);
            LeanTerResult const leanRes = formal_verification::checkFrozen(ledger, account, asset);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testCheckFrozenXRP()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();
        runCheckFrozen(
            *env.current(), alice.id(), Asset{xrpIssue()}, tesSUCCESS, "checkFrozen.xrp");
    }

    void
    testCheckFrozenIOU()
    {
        using namespace jtx;
        {  // trust line, no freeze
            Env env(*this);
            Account const gw("gw"), alice("alice");
            env.fund(XRP(1000), gw, alice);
            env.close();
            env(trust(alice, gw["USD"](1000)));
            env.close();
            runCheckFrozen(
                *env.current(),
                alice.id(),
                Asset{gw["USD"].issue()},
                tesSUCCESS,
                "checkFrozen.iou_unfrozen");
        }
        {  // global freeze
            Env env(*this);
            Account const gw("gw"), alice("alice");
            env.fund(XRP(1000), gw, alice);
            env.close();
            env(trust(alice, gw["USD"](1000)));
            env(fset(gw, asfGlobalFreeze));
            env.close();
            runCheckFrozen(
                *env.current(),
                alice.id(),
                Asset{gw["USD"].issue()},
                tecFROZEN,
                "checkFrozen.iou_global_freeze");
        }
        {  // individual freeze, exercising both lsfLowFreeze and lsfHighFreeze.
            // Median issuer: line(issuer, below) puts the issuer high (sets lsfHighFreeze);
            // line(issuer, above) puts the issuer low (sets lsfLowFreeze).
            Env env(*this);
            std::vector<Account> a{Account("frz1"), Account("frz2"), Account("frz3")};
            std::sort(a.begin(), a.end(), [](Account const& x, Account const& y) {
                return x.id() < y.id();
            });
            Account const below = a[0], issuer = a[1], above = a[2];
            env.fund(XRP(1000), below, issuer, above);
            env.close();
            env(trust(below, issuer["USD"](1000)));
            env(trust(above, issuer["USD"](1000)));
            env.close();
            env(trust(issuer, below["USD"](0), tfSetFreeze));
            env(trust(issuer, above["USD"](0), tfSetFreeze));
            env.close();
            runCheckFrozen(
                *env.current(),
                below.id(),
                Asset{issuer["USD"].issue()},
                tecFROZEN,
                "checkFrozen.iou_individual_freeze_high");
            runCheckFrozen(
                *env.current(),
                above.id(),
                Asset{issuer["USD"].issue()},
                tecFROZEN,
                "checkFrozen.iou_individual_freeze_low");
        }
    }

    void
    testCheckFrozenMPT()
    {
        using namespace jtx;
        {  // authorized holder, not locked
            Env env(*this);
            Account const gw("gw"), bob("bob");
            MPTTester mpt(
                env,
                gw,
                MPTInit{
                    .holders = {bob},
                    .fund = true,
                    .create =
                        MPTCreate{.authorize = std::vector<Account>{bob}, .flags = tfMPTCanLock}});
            runCheckFrozen(
                *env.current(),
                bob.id(),
                Asset{MPTIssue{mpt.issuanceID()}},
                tesSUCCESS,
                "checkFrozen.mpt_unlocked");
        }
        {  // issuance globally locked
            Env env(*this);
            Account const gw("gw"), bob("bob");
            MPTTester mpt(
                env,
                gw,
                MPTInit{
                    .holders = {bob},
                    .fund = true,
                    .create =
                        MPTCreate{.authorize = std::vector<Account>{bob}, .flags = tfMPTCanLock}});
            mpt.set({.account = gw, .flags = tfMPTLock});
            runCheckFrozen(
                *env.current(),
                bob.id(),
                Asset{MPTIssue{mpt.issuanceID()}},
                tecLOCKED,
                "checkFrozen.mpt_locked");
        }
        {  // individual holder lock (not a global lock)
            Env env(*this);
            Account const gw("gw"), bob("bob");
            MPTTester mpt(
                env,
                gw,
                MPTInit{
                    .holders = {bob},
                    .fund = true,
                    .create =
                        MPTCreate{.authorize = std::vector<Account>{bob}, .flags = tfMPTCanLock}});
            mpt.set({.account = gw, .holder = bob, .flags = tfMPTLock});
            runCheckFrozen(
                *env.current(),
                bob.id(),
                Asset{MPTIssue{mpt.issuanceID()}},
                tecLOCKED,
                "checkFrozen.mpt_holder_lock");
        }
    }

    void
    runTests() override
    {
        testCheckFrozenXRP();
        testCheckFrozenIOU();
        testCheckFrozenMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCheckFrozen, formal_verification, xrpl);

}  // namespace xrpl::test
