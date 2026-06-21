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

class LeanCanTransfer_test : public LedgerSuite
{
    void
    runCanTransfer(
        ReadView const& view,
        Asset const& asset,
        AccountID const& from,
        AccountID const& to,
        bool waive,
        TER expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = canTransfer(
                view, asset, from, to, waive ? WaiveMPTCanTransfer::Yes : WaiveMPTCanTransfer::No);
            LeanTerResult const leanRes =
                formal_verification::canTransfer(ledger, asset, from, to, waive);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    // canTransfer's vault-share recursion cross-checks requireAuth, so this helper is shared.
    void
    runRequireAuth(
        ReadView const& view,
        Asset const& asset,
        AccountID const& account,
        AuthType authType,
        TER expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = requireAuth(view, asset, account, authType);
            LeanTerResult const leanRes = formal_verification::requireAuth(
                ledger, asset, account, static_cast<uint8_t>(authType));
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testCanTransferXRP()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Env env(*this);
        env.fund(XRP(1000), alice, bob);
        env.close();
        auto const& view = *env.current();
        runCanTransfer(
            view, Asset{xrpIssue()}, alice.id(), bob.id(), false, tesSUCCESS, "canTransfer.xrp");
    }

    void
    testCanTransferIOU()
    {
        using namespace jtx;
        // Median issuer => two counterparties below it (issuer high -> lsfHighNoRipple)
        // and two above (issuer low -> lsfLowNoRipple)
        std::vector<Account> pool{
            Account("ctA"), Account("ctB"), Account("ctC"), Account("ctD"), Account("ctE")};
        std::sort(pool.begin(), pool.end(), [](Account const& a, Account const& b) {
            return a.id() < b.id();
        });
        Account const& iss = pool[2];        // DefaultRipple issuer
        Account const& highSet = pool[0];    // below issuer; lsfHighNoRipple set
        Account const& highClear = pool[1];  // below issuer; lsfHighNoRipple clear
        Account const& lowSet = pool[3];     // above issuer; lsfLowNoRipple set
        Account const& lowClear = pool[4];   // above issuer; lsfLowNoRipple clear
        Account const issNR("issNR");        // issuer without DefaultRipple
        Account const noln1("noln1");        // no trust lines
        Account const noln2("noln2");
        Env env(*this);
        env.fund(XRP(1000), pool[0], pool[1], pool[2], pool[3], pool[4], noln1, noln2);
        env.fund(XRP(1000), noripple(issNR));
        env(trust(iss, highSet["USD"](100), tfSetNoRipple));
        env(trust(iss, lowSet["USD"](100), tfSetNoRipple));
        env(trust(iss, highClear["USD"](100)));
        env(trust(iss, lowClear["USD"](100)));
        env.close();
        auto const& view = *env.current();
        Asset const usd{iss["USD"].issue()};
        Asset const usdNR{issNR["USD"].issue()};

        // issuer short-circuit (from == issuer || to == issuer)
        runCanTransfer(
            view, usd, iss.id(), highSet.id(), false, tesSUCCESS, "canTransfer.issuer_from");
        runCanTransfer(
            view, usd, highSet.id(), iss.id(), false, tesSUCCESS, "canTransfer.issuer_to");
        // isRippleDisabled (lines exist): both sides disabled -> terNO_RIPPLE
        runCanTransfer(
            view,
            usd,
            highSet.id(),
            lowSet.id(),
            false,
            terNO_RIPPLE,
            "canTransfer.both_no_ripple");
        // both sides enabled (flags clear) -> success
        runCanTransfer(
            view,
            usd,
            highClear.id(),
            lowClear.id(),
            false,
            tesSUCCESS,
            "canTransfer.both_ripple_ok");
        // one side disabled, one enabled -> success (the guard is AND)
        runCanTransfer(
            view, usd, highSet.id(), highClear.id(), false, tesSUCCESS, "canTransfer.high_mixed");
        runCanTransfer(
            view, usd, lowSet.id(), lowClear.id(), false, tesSUCCESS, "canTransfer.low_mixed");
        runCanTransfer(
            view, usd, highClear.id(), lowSet.id(), false, tesSUCCESS, "canTransfer.cross_mixed");
        // isRippleDisabled (no line): falls back to the issuer's DefaultRipple flag
        runCanTransfer(
            view,
            usd,
            noln1.id(),
            noln2.id(),
            false,
            tesSUCCESS,
            "canTransfer.no_line_default_ripple");
        runCanTransfer(
            view,
            usdNR,
            noln1.id(),
            noln2.id(),
            false,
            terNO_RIPPLE,
            "canTransfer.no_line_no_default_ripple");
    }

    void
    testCanTransferMPT()
    {
        using namespace jtx;
        {
            Account const bob("bob");
            Account const carol("carol");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const& view = *env.current();
            MPTIssue const absent{makeMptID(99, mpt.issuer().id())};

            // issuance absent -> tecOBJECT_NOT_FOUND
            runCanTransfer(
                view,
                Asset{absent},
                bob.id(),
                carol.id(),
                false,
                tecOBJECT_NOT_FOUND,
                "canTransfer.mpt_issuance_absent");
            // issuer short-circuit (from == issuer || to == issuer)
            runCanTransfer(
                view,
                mptAsset,
                mpt.issuer().id(),
                bob.id(),
                false,
                tesSUCCESS,
                "canTransfer.mpt_issuer_from");
            runCanTransfer(
                view,
                mptAsset,
                bob.id(),
                mpt.issuer().id(),
                false,
                tesSUCCESS,
                "canTransfer.mpt_issuer_to");
            // lsfMPTCanTransfer set -> success
            runCanTransfer(
                view,
                mptAsset,
                bob.id(),
                carol.id(),
                false,
                tesSUCCESS,
                "canTransfer.mpt_can_transfer");
        }
        {
            Account const bob("bob");
            Account const carol("carol");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}), .flags = 0}});
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const& view = *env.current();

            // waive short-circuits before the CanTransfer check
            runCanTransfer(
                view, mptAsset, bob.id(), carol.id(), true, tesSUCCESS, "canTransfer.mpt_waived");
            // lsfMPTCanTransfer not set -> tecNO_AUTH
            runCanTransfer(
                view,
                mptAsset,
                bob.id(),
                carol.id(),
                false,
                tecNO_AUTH,
                "canTransfer.mpt_no_can_transfer");
        }
    }

    void
    testVaultShareRecursion()
    {
        using namespace jtx;
        // underlying IOU asset -> recursion descends into an Issue
        {
            Env env(*this);
            Account const issuer("vissuer");
            Account const owner("vowner");
            Account const alice("valice");  // depositor; holds a share token
            Account const bob("vbob");      // third party
            Account const carol("vcarol");  // underlying line, but no shares
            Account const dave("vdave");    // no underlying line, no shares
            Vault const vault{env};
            env.fund(XRP(10000), issuer, owner, alice, bob, carol, dave);
            env.close();
            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(100000), alice);
            env.trust(asset(100000), bob);
            env.trust(asset(100000), carol);
            env(pay(issuer, alice, asset(1000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env.close();
            Asset const share{MPTIssue{env.le(keylet)->at(sfShareMPTID)}};
            auto const& view = *env.current();

            // third-party transfer inherits the underlying IOU's transferability
            runCanTransfer(
                view,
                share,
                alice.id(),
                bob.id(),
                false,
                tesSUCCESS,
                "canTransfer.vault_share_recurse");
            // recursion succeeds, depositor holds a share token
            runRequireAuth(
                view,
                share,
                alice.id(),
                AuthType::StrongAuth,
                tesSUCCESS,
                "requireAuth.vault_share_depositor");
            // recursion succeeds, but no share token -> strong auth fails
            runRequireAuth(
                view,
                share,
                carol.id(),
                AuthType::StrongAuth,
                tecNO_AUTH,
                "requireAuth.vault_share_no_token");
            // recursion itself fails (no underlying line) and propagates
            runRequireAuth(
                view,
                share,
                dave.id(),
                AuthType::StrongAuth,
                tecNO_LINE,
                "requireAuth.vault_share_recurse_fails");
            // proof the canTransfer recursion descends
            {
                Sandbox sb(&*env.current(), TapNone);
                auto const cur = issuer["IOU"].issue().currency;
                auto noRipple = [&](AccountID const& acct) {
                    auto ln = sb.peek(keylet::line(acct, issuer.id(), cur));
                    ln->setFieldU32(
                        sfFlags,
                        ln->getFieldU32(sfFlags) |
                            (issuer.id() > acct ? lsfHighNoRipple : lsfLowNoRipple));
                    sb.update(ln);
                };
                noRipple(alice.id());
                noRipple(bob.id());
                runCanTransfer(
                    sb,
                    share,
                    alice.id(),
                    bob.id(),
                    false,
                    terNO_RIPPLE,
                    "canTransfer.vault_share_recurse_no_ripple");
            }
        }
        // underlying MPT asset -> recursion descends into an MPTIssue
        {
            Env env(*this);
            Account const owner("mvowner");
            Account const alice("mvalice");  // depositor
            Account const bob("mvbob");      // third party
            MPTTester mpt(
                env,
                "mvgw",
                MPTInit{
                    .holders = {alice, bob},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock}});
            env.fund(XRP(10000), owner);
            PrettyAsset const underlying = mpt.issuanceID();
            mpt.pay(mpt.issuer(), alice, 1000);
            env.close();
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = underlying});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = underlying(100)}));
            env.close();
            Asset const share{MPTIssue{env.le(keylet)->at(sfShareMPTID)}};
            auto const& view = *env.current();

            runCanTransfer(
                view,
                share,
                alice.id(),
                bob.id(),
                false,
                tesSUCCESS,
                "canTransfer.vault_share_recurse_mpt");
            runRequireAuth(
                view,
                share,
                alice.id(),
                AuthType::StrongAuth,
                tesSUCCESS,
                "requireAuth.vault_share_depositor_mpt");
        }
    }

    // A private vault's share issuance carries sfDomainID + lsfMPTRequireAuth, so
    // requireAuth consults credentials::validDomain against the permissioned domain.
    void
    runTests() override
    {
        testCanTransferXRP();
        testCanTransferIOU();
        testCanTransferMPT();
        testVaultShareRecursion();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCanTransfer, formal_verification, xrpl);

}  // namespace xrpl::test
