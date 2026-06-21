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

class LeanCheckDeepFrozen_test : public LedgerSuite
{
    void
    runCheckDeepFrozen(
        ReadView const& view,
        AccountID const& account,
        Asset const& asset,
        TER expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = checkDeepFrozen(view, account, asset);
            LeanTerResult const leanRes =
                formal_verification::checkDeepFrozen(ledger, account, asset);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testCheckDeepFrozenXRP()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();
        runCheckDeepFrozen(
            *env.current(), alice.id(), Asset{xrpIssue()}, tesSUCCESS, "checkDeepFrozen.xrp");
    }

    void
    testCheckDeepFrozenIOU()
    {
        using namespace jtx;
        {  // regular freeze only is not a deep freeze
            Env env(*this);
            Account const gw("gw"), alice("alice");
            env.fund(XRP(1000), gw, alice);
            env.close();
            env(trust(alice, gw["USD"](1000)));
            env.close();
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            runCheckDeepFrozen(
                *env.current(),
                alice.id(),
                Asset{gw["USD"].issue()},
                tesSUCCESS,
                "checkDeepFrozen.iou_freeze_only");
        }
        {  // deep freeze, exercising both lsfLowDeepFreeze and lsfHighDeepFreeze (median issuer)
            Env env(*this);
            std::vector<Account> a{Account("dfz1"), Account("dfz2"), Account("dfz3")};
            std::sort(a.begin(), a.end(), [](Account const& x, Account const& y) {
                return x.id() < y.id();
            });
            Account const below = a[0], issuer = a[1], above = a[2];
            env.fund(XRP(1000), below, issuer, above);
            env.close();
            env(trust(below, issuer["USD"](1000)));
            env(trust(above, issuer["USD"](1000)));
            env.close();
            env(trust(issuer, below["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env(trust(issuer, above["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();
            runCheckDeepFrozen(
                *env.current(),
                below.id(),
                Asset{issuer["USD"].issue()},
                tecFROZEN,
                "checkDeepFrozen.iou_deep_freeze_high");
            runCheckDeepFrozen(
                *env.current(),
                above.id(),
                Asset{issuer["USD"].issue()},
                tecFROZEN,
                "checkDeepFrozen.iou_deep_freeze_low");
        }
        {  // deep freeze flag without a regular freeze flag (staged): isDeepFrozen must read
            // the deep flag specifically, so checkDeepFrozen is still frozen.
            Env env(*this);
            Account const gw("gw"), alice("alice");
            env.fund(XRP(1000), gw, alice);
            env.close();
            env(trust(alice, gw["USD"](1000)));
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            Issue const usd = gw["USD"].issue();
            auto line = sb.peek(keylet::line(alice.id(), gw.id(), usd.currency));
            std::uint32_t const deepFlag =
                gw.id() < alice.id() ? lsfLowDeepFreeze : lsfHighDeepFreeze;
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) | deepFlag);
            sb.update(line);
            runCheckDeepFrozen(
                sb, alice.id(), Asset{usd}, tecFROZEN, "checkDeepFrozen.iou_deep_no_freeze");
        }
    }

    void
    testCheckDeepFrozenMPT()
    {
        using namespace jtx;
        // deep frozen delegates to the locked check (tecLOCKED vs tesSUCCESS)
        {  // not locked
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
            runCheckDeepFrozen(
                *env.current(),
                bob.id(),
                Asset{MPTIssue{mpt.issuanceID()}},
                tesSUCCESS,
                "checkDeepFrozen.mpt_unlocked");
        }
        {  // locked
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
            runCheckDeepFrozen(
                *env.current(),
                bob.id(),
                Asset{MPTIssue{mpt.issuanceID()}},
                tecLOCKED,
                "checkDeepFrozen.mpt_locked");
        }
    }

    void
    runTests() override
    {
        testCheckDeepFrozenXRP();
        testCheckDeepFrozenIOU();
        testCheckDeepFrozenMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCheckDeepFrozen, formal_verification, xrpl);

}  // namespace xrpl::test
