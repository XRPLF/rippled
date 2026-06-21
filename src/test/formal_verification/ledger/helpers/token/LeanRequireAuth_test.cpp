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

class LeanRequireAuth_test : public LedgerSuite
{
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
    testRequireAuthXRP()
    {
        using namespace jtx;
        Account const alice("alice");
        Env env(*this);
        env.fund(XRP(1000), alice);
        env.close();
        auto const& view = *env.current();
        runRequireAuth(
            view, Asset{xrpIssue()}, alice.id(), AuthType::Legacy, tesSUCCESS, "requireAuth.xrp");
    }

    void
    testRequireAuthIOU()
    {
        using namespace jtx;
        Account const gw("gw");          // no RequireAuth
        Account const gwAuth("gwAuth");  // asfRequireAuth
        Account const alice("alice");    // authorized line to gwAuth, plain line to gw
        Account const bob("bob");        // unauthorized line to gwAuth
        Account const carol("carol");    // no trust lines
        Env env(*this);
        env.fund(XRP(1000), gw, gwAuth, alice, bob, carol);
        env.close();
        env(fset(gwAuth, asfRequireAuth));
        env(trust(alice, gw["USD"](100)));
        env(trust(alice, gwAuth["USD"](100)));
        env(trust(gwAuth, alice["USD"](0), tfSetfAuth));  // gwAuth authorizes alice
        env(trust(bob, gwAuth["USD"](100)));
        env.close();
        auto const& view = *env.current();
        Asset const usd{gw["USD"].issue()};
        Asset const usdAuth{gwAuth["USD"].issue()};

        // isXRP || account == issuer -> tesSUCCESS
        runRequireAuth(
            view, usd, gw.id(), AuthType::Legacy, tesSUCCESS, "requireAuth.account_is_issuer");
        // no line + StrongAuth -> tecNO_LINE
        runRequireAuth(
            view, usd, carol.id(), AuthType::StrongAuth, tecNO_LINE, "requireAuth.strong_no_line");
        // issuer requires auth, line present -> authorized tesSUCCESS / unauthorized tecNO_AUTH
        runRequireAuth(
            view, usdAuth, alice.id(), AuthType::Legacy, tesSUCCESS, "requireAuth.authorized");
        runRequireAuth(
            view, usdAuth, bob.id(), AuthType::Legacy, tecNO_AUTH, "requireAuth.unauthorized");
        // issuer requires auth, no line -> tecNO_LINE
        runRequireAuth(
            view,
            usdAuth,
            carol.id(),
            AuthType::Legacy,
            tecNO_LINE,
            "requireAuth.require_auth_no_line");
        // issuer does not require auth -> tesSUCCESS (weak no-line, or line present)
        runRequireAuth(
            view,
            usd,
            carol.id(),
            AuthType::WeakAuth,
            tesSUCCESS,
            "requireAuth.weak_no_line_no_auth_required");
        runRequireAuth(
            view,
            usd,
            alice.id(),
            AuthType::Legacy,
            tesSUCCESS,
            "requireAuth.legacy_line_no_auth_required");
    }

    void
    testRequireAuthMPT()
    {
        using namespace jtx;
        {
            Account const bob("bob");  // holds a token
            Account const dan("dan");  // no token
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            env.fund(XRP(1000), dan);
            env.close();
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const& view = *env.current();
            MPTIssue const absent{makeMptID(99, mpt.issuer().id())};

            // issuance absent -> tecOBJECT_NOT_FOUND
            runRequireAuth(
                view,
                Asset{absent},
                bob.id(),
                AuthType::Legacy,
                tecOBJECT_NOT_FOUND,
                "requireAuth.mpt_issuance_absent");
            // account == issuer -> tesSUCCESS
            runRequireAuth(
                view,
                mptAsset,
                mpt.issuer().id(),
                AuthType::Legacy,
                tesSUCCESS,
                "requireAuth.mpt_account_is_issuer");
            // no token: Strong/Legacy fail, Weak passes (issuance does not require auth)
            runRequireAuth(
                view,
                mptAsset,
                dan.id(),
                AuthType::StrongAuth,
                tecNO_AUTH,
                "requireAuth.mpt_no_token_strong");
            runRequireAuth(
                view,
                mptAsset,
                dan.id(),
                AuthType::WeakAuth,
                tesSUCCESS,
                "requireAuth.mpt_no_token_weak");
            // token present, issuance does not require auth -> tesSUCCESS
            runRequireAuth(
                view, mptAsset, bob.id(), AuthType::Legacy, tesSUCCESS, "requireAuth.mpt_token");
        }
        {
            Account const bob("bob");      // authorized
            Account const carol("carol");  // opted in, not authorized
            Account const dan("dan");      // no token
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{.holders = {bob, carol}, .create = MPTCreate{.flags = tfMPTRequireAuth}});
            env.fund(XRP(1000), dan);
            mpt.authorize({.account = bob});    // bob opts in
            mpt.authorize({.holder = bob});     // issuer authorizes bob
            mpt.authorize({.account = carol});  // carol opts in (stays unauthorized)
            env.close();
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const& view = *env.current();

            // no token + Legacy -> tecNO_AUTH
            runRequireAuth(
                view,
                mptAsset,
                dan.id(),
                AuthType::Legacy,
                tecNO_AUTH,
                "requireAuth.mpt_require_auth_no_token");
            // pseudo-account is implicitly authorized (WeakAuth skips the missing-token check)
            {
                Sandbox sb(&*env.current(), TapNone);
                auto sle = sb.peek(keylet::account(dan.id()));
                sle->setFieldH256(sfAMMID, uint256{1});  // mark dan a pseudo-account
                sb.update(sle);
                runRequireAuth(
                    sb,
                    mptAsset,
                    dan.id(),
                    AuthType::WeakAuth,
                    tesSUCCESS,
                    "requireAuth.mpt_pseudo_account");
            }
            // token present, issuance requires auth: unauthorized -> tecNO_AUTH
            runRequireAuth(
                view,
                mptAsset,
                carol.id(),
                AuthType::Legacy,
                tecNO_AUTH,
                "requireAuth.mpt_require_auth_unauthorized");
            // token present + authorized -> tesSUCCESS
            runRequireAuth(
                view,
                mptAsset,
                bob.id(),
                AuthType::Legacy,
                tesSUCCESS,
                "requireAuth.mpt_require_auth_authorized");
        }
    }

    // A vault share is an MPT whose issuer is the vault pseudo-account (carrying
    // sfVaultID) and whose issuance carries sfReferenceHolding. Both requireAuth
    // and canTransfer recurse through these into the vault's underlying asset.
    void
    testRequireAuthDomain()
    {
        using namespace jtx;
        Env env(*this);
        Account const issuer("dissuer");
        Account const owner("downer");
        Account const pdOwner("dpdowner");
        Account const credIssuer("dcredissuer");
        Account const authed("dauthed");  // holds an accepted credential
        Account const noauth("dnoauth");  // no credential
        Account const former("dformer");  // deposits, then loses its credential
        std::string const credType = "vaultcred";
        Vault const vault{env};
        env.fund(XRP(10000), issuer, owner, pdOwner, credIssuer, authed, noauth, former);
        env.close();
        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(100000), owner);
        env(pay(issuer, owner, asset(1000)));
        env.trust(asset(100000), former);
        env(pay(issuer, former, asset(1000)));
        env.close();

        // private vault -> share issuance is created with lsfMPTRequireAuth
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();

        // a permissioned domain accepting (credIssuer, credType)
        pdomain::Credentials const creds{{.issuer = credIssuer, .credType = credType}};
        env(pdomain::setTx(pdOwner, creds));
        uint256 const domainId = pdomain::getNewDomain(env.meta());
        env.close();
        auto setv = vault.set({.owner = owner, .id = keylet.key});
        setv[sfDomainID] = to_string(domainId);
        env(setv);
        env.close();

        // authed accepts a credential in the domain; noauth gets none
        env(credentials::create(authed, credIssuer, credType));
        env(credentials::accept(authed, credIssuer, credType));
        env.close();

        // former gets authorized, deposits (-> share token), then loses the credential
        env(credentials::create(former, credIssuer, credType));
        env(credentials::accept(former, credIssuer, credType));
        env.close();
        env(vault.deposit({.depositor = former, .id = keylet.key, .amount = asset(100)}));
        env.close();
        env(credentials::deleteCred(credIssuer, former, credIssuer, credType));
        env.close();

        Asset const share{MPTIssue{env.le(keylet)->at(sfShareMPTID)}};
        auto const& view = *env.current();

        // weak auth + no share token reaches the DomainID check (validDomain)
        runRequireAuth(
            view,
            share,
            authed.id(),
            AuthType::WeakAuth,
            tesSUCCESS,
            "requireAuth.vault_domain_authorized");
        runRequireAuth(
            view,
            share,
            noauth.id(),
            AuthType::WeakAuth,
            tecNO_AUTH,
            "requireAuth.vault_domain_unauthorized");
        // present + individually-authorized share token: validDomain fails, but the fall-through
        // reaches the auth-flag check and succeeds
        {
            Sandbox sb(&*env.current(), TapNone);
            auto tok = sb.peek(keylet::mptoken(share.get<MPTIssue>().getMptID(), former.id()));
            tok->setFieldU32(sfFlags, tok->getFieldU32(sfFlags) | lsfMPTAuthorized);
            sb.update(tok);
            runRequireAuth(
                sb,
                share,
                former.id(),
                AuthType::WeakAuth,
                tesSUCCESS,
                "requireAuth.vault_domain_has_token");
        }
    }

    void
    runTests() override
    {
        testRequireAuthXRP();
        testRequireAuthIOU();
        testRequireAuthMPT();
        testRequireAuthDomain();
    }
};

BEAST_DEFINE_TESTSUITE(LeanRequireAuth, formal_verification, xrpl);

}  // namespace xrpl::test
