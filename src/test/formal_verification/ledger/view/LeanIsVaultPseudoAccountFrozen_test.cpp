#include <test/formal_verification/ffi/ledger/ViewFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/mpt.h>
#include <test/jtx/vault.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>

namespace xrpl::test {

using namespace formal_verification;

class LeanIsVaultPseudoAccountFrozen_test : public LedgerSuite
{
    void
    runIsVaultPseudoAccountFrozen(
        ReadView const& view,
        AccountID const& account,
        MPTIssue const& mptShare,
        std::uint8_t depth,
        bool expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            bool const cpp = isVaultPseudoAccountFrozen(view, account, mptShare, depth);
            LeanBoolResult const leanRes =
                formal_verification::isVaultPseudoAccountFrozen(ledger, account, mptShare, depth);
            BEAST_EXPECT(cpp == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cpp);
        });
    }

    void
    testIsVaultPseudoAccountFrozen()
    {
        using namespace jtx;
        {  // issuance absent -> not a vault share, not frozen
            Env env(*this);
            Account const gw("gw");
            env.fund(XRP(1000), gw);
            env.close();
            MPTIssue const absent{makeMptID(99, gw.id())};
            runIsVaultPseudoAccountFrozen(
                *env.current(), gw.id(), absent, 0, false, "isVaultPseudoAccountFrozen.absent");
        }
        {  // plain MPT: issuer is a normal account, no sfReferenceHolding, no sfVaultID
            Env env(*this);
            Account const gw("gw");
            MPTTester mpt(
                env, gw, MPTInit{.fund = true, .create = MPTCreate{.flags = tfMPTCanTransfer}});
            runIsVaultPseudoAccountFrozen(
                *env.current(),
                gw.id(),
                MPTIssue{mpt.issuanceID()},
                0,
                false,
                "isVaultPseudoAccountFrozen.plain_mpt");
        }
        {  // real vault share over an IOU underlying (issuance carries sfReferenceHolding)
            Env env(*this);
            Account const issuer("vissuer"), owner("vowner"), alice("valice");
            Vault vault{env};
            env.fund(XRP(10000), issuer, owner, alice);
            env.close();
            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(100000), alice);
            env(pay(issuer, alice, asset(1000)));
            env.close();
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env.close();
            MPTIssue const share{env.le(keylet)->at(sfShareMPTID)};

            // underlying not frozen
            runIsVaultPseudoAccountFrozen(
                *env.current(), alice.id(), share, 0, false, "isVaultPseudoAccountFrozen.unfrozen");
            // individual freeze of alice's underlying line -> isAnyFrozen via isIndividualFrozen
            env(trust(issuer, alice["IOU"](0), tfSetFreeze));
            env.close();
            runIsVaultPseudoAccountFrozen(
                *env.current(),
                alice.id(),
                share,
                0,
                true,
                "isVaultPseudoAccountFrozen.individual_freeze");
            // global freeze of the underlying issuer -> isAnyFrozen via isGlobalFrozen
            env(fset(issuer, asfGlobalFreeze));
            env.close();
            runIsVaultPseudoAccountFrozen(
                *env.current(),
                alice.id(),
                share,
                0,
                true,
                "isVaultPseudoAccountFrozen.global_freeze");
        }
        {  // sfReferenceHolding erased -> C++ falls back to the issuer->vaultID chain,
            // matching the model's only path. Underlying global-frozen -> true.
            Env env(*this);
            Account const issuer("vissuer"), owner("vowner"), alice("valice");
            Vault vault{env};
            env.fund(XRP(10000), issuer, owner, alice);
            env.close();
            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(100000), alice);
            env(pay(issuer, alice, asset(1000)));
            env.close();
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env(fset(issuer, asfGlobalFreeze));
            env.close();
            MPTIssue const share{env.le(keylet)->at(sfShareMPTID)};

            Sandbox sb(&*env.current(), TapNone);
            auto iss = sb.peek(keylet::mptIssuance(share.getMptID()));
            iss->makeFieldAbsent(sfReferenceHolding);
            sb.update(iss);
            runIsVaultPseudoAccountFrozen(
                sb, alice.id(), share, 0, true, "isVaultPseudoAccountFrozen.no_reference_holding");
        }
    }

    void
    runTests() override
    {
        testIsVaultPseudoAccountFrozen();
    }
};

BEAST_DEFINE_TESTSUITE(LeanIsVaultPseudoAccountFrozen, formal_verification, xrpl);

}  // namespace xrpl::test
