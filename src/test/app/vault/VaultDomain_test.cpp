#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/deposit.h>
#include <test/jtx/flags.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultDomain_test : public VaultTestBase
{
private:
    void
    testWithDomainCheck()
    {
        using namespace test::jtx;

        testcase("private vault");

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const charlie{"charlie"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer1{"credIssuer1"};
        Account const credIssuer2{"credIssuer2"};
        std::string const credType = "credential";
        Vault const vault{env};
        env.fund(XRP(1000), issuer, owner, depositor, charlie, pdOwner, credIssuer1, credIssuer2);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(issuer, asfAllowTrustLineClawback));

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(500)));
        env.trust(asset(1000), depositor);
        env(pay(issuer, depositor, asset(500)));
        env.trust(asset(1000), charlie);
        env(pay(issuer, charlie, asset(5)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();
        BEAST_EXPECT(env.le(keylet));

        {
            testcase("private vault owner can deposit");
            auto tx = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx);
        }

        {
            testcase("private vault depositor not authorized yet");
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
        }

        {
            testcase("private vault cannot set non-existing domain");
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
            env(tx, Ter{tecOBJECT_NOT_FOUND});
        }

        {
            testcase("private vault set domainId");

            {
                pdomain::Credentials const credentials1{
                    {.issuer = credIssuer1, .credType = credType}};

                env(pdomain::setTx(pdOwner, credentials1));
                auto const domainId1 = [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
                    return pdomain::getNewDomain(env.meta());
                }();

                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId1);
                env(tx);
                env.close();

                // Update domain second time, should be harmless
                env(tx);
                env.close();
            }

            {
                pdomain::Credentials const credentials{
                    {.issuer = credIssuer1, .credType = credType},
                    {.issuer = credIssuer2, .credType = credType}};

                env(pdomain::setTx(pdOwner, credentials));
                auto const domainId = [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
                    return pdomain::getNewDomain(env.meta());
                }();

                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId);
                env(tx);
                env.close();

                // Should be idempotent
                tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId);
                env(tx);
                env.close();
            }
        }

        {
            testcase("private vault depositor still not authorized");
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();
        }

        auto const credKeylet = credentials::keylet(depositor, credIssuer1, credType);
        {
            testcase("private vault depositor now authorized");
            env(credentials::create(depositor, credIssuer1, credType));
            env(credentials::accept(depositor, credIssuer1, credType));
            env(credentials::create(charlie, credIssuer1, credType));
            // charlie's credential not accepted
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle != nullptr);

            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = charlie, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();
        }

        {
            testcase("private vault depositor lost authorization");
            env(credentials::deleteCred(credIssuer1, depositor, credIssuer1, credType));
            env(credentials::deleteCred(credIssuer1, charlie, credIssuer1, credType));
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle == nullptr);

            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();
        }

        auto const shares = [&env, keylet = keylet, this]() -> Asset {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return MPTIssue(vault->at(sfShareMPTID));
        }();

        {
            testcase("private vault expired authorization");
            uint32_t const closeTime =
                env.current()->header().parentCloseTime.time_since_epoch().count();
            {
                auto tx0 = credentials::create(depositor, credIssuer2, credType);
                tx0[sfExpiration] = closeTime + 20;
                env(tx0);
                tx0 = credentials::create(charlie, credIssuer2, credType);
                tx0[sfExpiration] = closeTime + 20;
                env(tx0);
                env.close();

                env(credentials::accept(depositor, credIssuer2, credType));
                env(credentials::accept(charlie, credIssuer2, credType));
                env.close();
            }

            {
                auto tx1 =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                env(tx1);
                env.close();

                auto const tokenKeylet =
                    keylet::mptoken(shares.get<MPTIssue>().getMptID(), depositor.id());
                BEAST_EXPECT(env.le(tokenKeylet) != nullptr);
            }

            {
                // time advance
                env.close();
                env.close();
                env.close();

                auto const credsKeylet = credentials::keylet(depositor, credIssuer2, credType);
                BEAST_EXPECT(env.le(credsKeylet) != nullptr);

                auto tx2 =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1)});
                env(tx2, Ter{tecEXPIRED});
                env.close();

                BEAST_EXPECT(env.le(credsKeylet) == nullptr);
            }

            {
                auto const credsKeylet = credentials::keylet(charlie, credIssuer2, credType);
                BEAST_EXPECT(env.le(credsKeylet) != nullptr);
                auto const tokenKeylet =
                    keylet::mptoken(shares.get<MPTIssue>().getMptID(), charlie.id());
                BEAST_EXPECT(env.le(tokenKeylet) == nullptr);

                auto tx3 =
                    vault.deposit({.depositor = charlie, .id = keylet.key, .amount = asset(2)});
                env(tx3, Ter{tecEXPIRED});

                env.close();
                BEAST_EXPECT(env.le(credsKeylet) == nullptr);
                BEAST_EXPECT(env.le(tokenKeylet) == nullptr);
            }
        }

        {
            testcase("private vault reset domainId");
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = "0";
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();

            tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();

            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(0)});
            env(tx);

            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(0)});
            env(tx);
            env.close();

            tx = vault.del({
                .owner = owner,
                .id = keylet.key,
            });
            env(tx);
        }
    }

    void
    testDomainLossAfterAcquisition()
    {
        using namespace test::jtx;

        testcase("private vault share transfer after depositor loses domain");

        // The "Private Vault - Access Control Rules" spec requires that a holder who
        // loses Layer 2 (Permissioned Domain membership) after acquiring shares be
        // blocked from sending them onward, by P2P transfer or DEX offer, the same
        // way a brand-new never-authorized holder is blocked. Only withdrawal to
        // self is meant to stay open.
        //
        // For a domain-gated share MPToken, requireAuth()'s escape hatch for
        // holders who already have an MPToken (MPTokenHelpers.cpp) only applies to
        // the classic explicit-issuer-authorization flag, which
        // enforceMPTokenAuthorization documents as "meaningless" for
        // domain-authorized holders and never sets. So a stale MPToken does not
        // carry authorization forward once the account's domain credential is
        // gone, and both actions below are correctly blocked.

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const bob{"bob"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        std::string const credType = "credential";
        Vault const vault{env};
        env.fund(XRP(1000), issuer, owner, depositor, bob, pdOwner, credIssuer);
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(500)));
        env.trust(asset(1000), depositor);
        env(pay(issuer, depositor, asset(500)));
        env.trust(asset(1000), bob);
        env(pay(issuer, bob, asset(500)));
        env.close();

        // Transferable shares (no tfVaultShareNonTransferable): sections 3.3/3.4 of
        // the spec (DEX trading / P2P transfer) only apply to transferable shares.
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();

        pdomain::Credentials const credentials{{.issuer = credIssuer, .credType = credType}};
        env(pdomain::setTx(pdOwner, credentials));
        auto const domainId = [&]() {
            auto tx = env.tx()->getJson(JsonOptions::Values::None);
            return pdomain::getNewDomain(env.meta());
        }();
        {
            auto domainTx = vault.set({.owner = owner, .id = keylet.key});
            domainTx[sfDomainID] = to_string(domainId);
            env(domainTx);
            env.close();
        }

        // Both depositor and bob acquire domain membership and deposit, so each
        // ends up with an authorized share MPToken.
        env(credentials::create(depositor, credIssuer, credType));
        env(credentials::accept(depositor, credIssuer, credType));
        env(credentials::create(bob, credIssuer, credType));
        env(credentials::accept(bob, credIssuer, credType));
        env.close();

        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)}));
        env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(100)}));
        env.close();

        auto const shares = [&env, keylet = keylet, this]() -> PrettyAsset {
            auto const sle = env.le(keylet);
            BEAST_EXPECT(sle != nullptr);
            return MPTIssue(sle->at(sfShareMPTID));
        }();

        // Depositor loses Layer 2: their Permissioned Domain credential is revoked.
        auto const credKeylet = credentials::keylet(depositor, credIssuer, credType);
        env(credentials::deleteCred(credIssuer, depositor, credIssuer, credType));
        env.close();
        BEAST_EXPECT(env.le(credKeylet) == nullptr);

        // Sanity check, mirrors testWithDomainCheck's "not authorized yet" case: a
        // brand-new depositor with no MPToken yet is still correctly blocked. The
        // gap below is specific to holders who already hold shares.
        {
            Account const charlie{"charlie"};
            env.fund(XRP(1000), charlie);
            env.close();
            auto depTx =
                vault.deposit({.depositor = charlie, .id = keylet.key, .amount = asset(1)});
            env(depTx, Ter{tecNO_AUTH});
        }

        // P2P transfer: spec section 3.4 requires this blocked once Layer 2 is
        // lost, and it is.
        env(pay(depositor, bob, shares(1)), Ter{tecNO_AUTH});
        env.close();

        // DEX/CLOB: spec section 3.3 requires the seller leg blocked the same way.
        // The offer can't even be created: preclaim treats the seller as
        // unfunded once their share balance reads as zero for auth purposes.
        env(offer(depositor, XRP(1), shares(1)), Ter{tecUNFUNDED_OFFER});
        env.close();
        BEAST_EXPECT(expectOffers(env, depositor, 0));
    }

    void
    testDomainCheckBuyerSideOffer()
    {
        using namespace test::jtx;

        testcase("private vault share purchase via DEX requires buyer domain membership");

        // The "Private Vault - Access Control Rules" spec requires the buyer leg
        // of a DEX trade in private-vault shares to hold Layer 1 and Layer 2 as
        // well, not just the seller.

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const bob{"bob"};
        Account const charlie{"charlie"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        std::string const credType = "credential";
        Vault const vault{env};
        env.fund(XRP(1000), issuer, owner, bob, charlie, pdOwner, credIssuer);
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(500)));
        env.trust(asset(1000), bob);
        env(pay(issuer, bob, asset(500)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();

        pdomain::Credentials const credentials{{.issuer = credIssuer, .credType = credType}};
        env(pdomain::setTx(pdOwner, credentials));
        auto const domainId = [&]() {
            auto tx = env.tx()->getJson(JsonOptions::Values::None);
            return pdomain::getNewDomain(env.meta());
        }();
        {
            auto domainTx = vault.set({.owner = owner, .id = keylet.key});
            domainTx[sfDomainID] = to_string(domainId);
            env(domainTx);
            env.close();
        }

        // Only bob joins the domain and deposits; charlie never does.
        env(credentials::create(bob, credIssuer, credType));
        env(credentials::accept(bob, credIssuer, credType));
        env.close();
        env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(100)}));
        env.close();

        auto const shares = [&env, keylet = keylet, this]() -> PrettyAsset {
            auto const sle = env.le(keylet);
            BEAST_EXPECT(sle != nullptr);
            return MPTIssue(sle->at(sfShareMPTID));
        }();

        // Bob (domain member, holds shares) rests a sell offer.
        env(offer(bob, XRP(1), shares(1)));
        env.close();
        BEAST_EXPECT(expectOffers(env, bob, 1));

        // Charlie never held the domain credential. Buying shares via a
        // crossing offer must be blocked the same way a direct MPTokenAuthorize
        // + pay attempt already is (see testWithDomainChecXRP's "cannot pay
        // shares to 3rd party"): checkAcceptAsset() rejects the offer outright
        // in preclaim, before any funding check is even reached.
        env(offer(charlie, shares(1), XRP(1)), Ter{tecNO_AUTH});
        env.close();
        BEAST_EXPECT(expectOffers(env, bob, 1));
        BEAST_EXPECT(expectOffers(env, charlie, 0));
    }

    void
    testWithDomainChecXRP()
    {
        using namespace test::jtx;

        testcase("private XRP vault");

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const alice{"charlie"};
        std::string const credType = "credential";
        Vault const vault{env};
        env.fund(XRP(100000), owner, depositor, alice);
        env.close();

        PrettyAsset const asset = xrpIssue();
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();

        auto const [vaultAccount, issuanceId] =
            [&env, keylet = keylet, this]() -> std::tuple<AccountID, UInt192> {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return {vault->at(sfAccount), vault->at(sfShareMPTID)};
        }();
        BEAST_EXPECT(env.le(keylet::account(vaultAccount)));
        BEAST_EXPECT(env.le(keylet::mptokenIssuance(issuanceId)));
        PrettyAsset const shares{issuanceId};

        {
            testcase("private XRP vault owner can deposit");
            auto tx = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();
        }

        {
            testcase("private XRP vault cannot pay shares to depositor yet");
            env(pay(owner, depositor, shares(1)), Ter{tecNO_AUTH});
        }

        {
            testcase("private XRP vault depositor not authorized yet");
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
        }

        {
            testcase("private XRP vault set DomainID");
            pdomain::Credentials const credentials{{.issuer = owner, .credType = credType}};

            env(pdomain::setTx(owner, credentials));
            auto const domainId = [&]() {
                auto tx = env.tx()->getJson(JsonOptions::Values::None);
                return pdomain::getNewDomain(env.meta());
            }();

            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = to_string(domainId);
            env(tx);
            env.close();
        }

        auto const credKeylet = credentials::keylet(depositor, owner, credType);
        {
            testcase("private XRP vault depositor now authorized");
            env(credentials::create(depositor, owner, credType));
            env(credentials::accept(depositor, owner, credType));
            env.close();

            BEAST_EXPECT(env.le(credKeylet));
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();
        }

        {
            testcase("private XRP vault can pay shares to depositor");
            env(pay(owner, depositor, shares(1)));
        }

        {
            testcase("private XRP vault cannot pay shares to 3rd party");
            json::Value jv;
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfMPTokenIssuanceID] = to_string(issuanceId);
            env(jv);
            env.close();

            env(pay(owner, alice, shares(1)), Ter{tecNO_AUTH});
        }
    }

    // Withdrawing out of a private vault to a third party requires both the
    // submitter and the destination to be members of the vault's permissioned
    // domain. Withdrawal to self is exempt: revoking vault access must not
    // trap already deposited funds. The asset issuer is exempt as a
    // destination, so that frozen assets can always be returned.
    void
    testVaultWithdrawPrivateDestinationDomain(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_4_0];
        testcase(
            std::string{"VaultWithdraw private vault destination domain check"} +
            (withFix ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const beneficiary{"beneficiary"};
        Account const outsider{"outsider"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        std::string const credType = "credential";

        Env env{*this, features};
        Vault const vault{env};

        env.fund(
            XRP(100'000), issuer, owner, depositor, beneficiary, outsider, pdOwner, credIssuer);
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        // Everyone holds Layer 1 (asset) permission, so anything blocked below
        // is blocked by the Layer 2 (vault) check alone.
        for (auto const& account : {owner, depositor, beneficiary, outsider})
        {
            env.trust(asset(1'000'000), account);
            env(pay(issuer, account, asset(10'000)));
        }
        env.close();

        auto const domainId = [&]() {
            pdomain::Credentials const credentials{{.issuer = credIssuer, .credType = credType}};
            env(pdomain::setTx(pdOwner, credentials));
            env.close();
            return pdomain::getNewDomain(env.meta());
        }();

        auto const joinDomain = [&](Account const& account) {
            env(credentials::create(account, credIssuer, credType));
            env(credentials::accept(account, credIssuer, credType));
            env.close();
        };
        joinDomain(depositor);
        joinDomain(beneficiary);

        auto [createTx, keylet] =
            vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(createTx);
        env.close();

        {
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = to_string(domainId);
            env(tx);
            env.close();
        }

        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1'000)}));
        env.close();

        auto const withdrawTo = [&, keylet = keylet](Account const& destination) {
            auto tx =
                vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(1)});
            tx[sfDestination] = destination.human();
            return tx;
        };

        {
            // Destination holds both layers of permission.
            env(withdrawTo(beneficiary));
            env.close();
        }

        {
            // Destination may hold the asset but was never let into the vault.
            env(withdrawTo(outsider), Ter(withFix ? TER(tecNO_AUTH) : TER(tesSUCCESS)));
            env.close();
        }

        {
            // The asset issuer can always receive, to keep the recovery path
            // for frozen assets open.
            env(withdrawTo(issuer));
            env.close();
        }

        {
            // The vault owner gets no special treatment as a destination: it
            // is a third party like any other and needs domain membership.
            env(withdrawTo(owner), Ter(withFix ? TER(tecNO_AUTH) : TER(tesSUCCESS)));
            env.close();
        }

        {
            // Withdrawal to self needs no Destination and stays unaffected.
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
            env.close();
        }

        {
            // Naming yourself as the Destination is still a withdrawal to self.
            env(withdrawTo(depositor));
            env.close();
        }

        {
            testcase(
                std::string{"VaultWithdraw private vault submitter lost vault access"} +
                (withFix ? " (fixCleanup3_4_0)" : " (pre-fix)"));

            env(credentials::deleteCred(credIssuer, depositor, credIssuer, credType));
            env.close();

            // The exit of last resort: the submitter lost vault access but
            // must still be able to redeem its own shares.
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
            env.close();

            // Moving funds to anyone else is not allowed any more, even to a
            // destination that is itself a domain member.
            env(withdrawTo(beneficiary), Ter(withFix ? TER(tecNO_AUTH) : TER(tesSUCCESS)));
            env.close();

            // Returning assets to the issuer stays open regardless.
            env(withdrawTo(issuer));
            env.close();
        }

        {
            testcase(
                std::string{"VaultWithdraw private vault with no domain set"} +
                (withFix ? " (fixCleanup3_4_0)" : " (pre-fix)"));

            // Give the submitter its vault access back first, so that the
            // vault having no domain is the only reason left to refuse.
            env(credentials::create(depositor, credIssuer, credType));
            env(credentials::accept(depositor, credIssuer, credType));
            env.close();

            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = "0";
            env(tx);
            env.close();

            // Clearing the domain leaves the vault with nobody it considers
            // authorized, so a third-party destination cannot qualify even
            // though both ends of the payout hold a credential.
            env(withdrawTo(beneficiary), Ter(withFix ? TER(tecNO_AUTH) : TER(tesSUCCESS)));
            env.close();

            // The two exempt paths survive the domain going away.
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
            env.close();

            env(withdrawTo(issuer));
            env.close();
        }

        {
            testcase(
                std::string{"VaultWithdraw public vault destination unaffected"} +
                (withFix ? " (fixCleanup3_4_0)" : " (pre-fix)"));

            auto [publicTx, publicKeylet] = vault.create({.owner = owner, .asset = asset});
            env(publicTx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = publicKeylet.key, .amount = asset(100)}));
            env.close();

            auto tx =
                vault.withdraw({.depositor = owner, .id = publicKeylet.key, .amount = asset(1)});
            tx[sfDestination] = outsider.human();
            env(tx);
            env.close();
        }
    }

    void
    testWithdrawCredentialDepositPreauth(FeatureBitset features)
    {
        testcase(
            "withdraw with credential-based deposit preauth " +
            std::string{features[fixCleanup3_4_0] ? "post-fix" : "pre-fix"});
        using namespace test::jtx;
        using namespace std::chrono_literals;

        bool const fixEnabled = features[fixCleanup3_4_0];

        Env env{*this, features};

        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const dest{"dest"};
        Account const credIssuer{"credIssuer"};
        char const credType[] = "abcde";

        env.fund(XRP(1000), owner, depositor, dest, credIssuer);
        env(fset(dest, asfDepositAuth));
        env.close();

        PrettyAsset const asset{xrpIssue(), 1'000'000};
        Vault vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)}));
        env.close();

        auto withdrawToDest = [&]() {
            auto wtx =
                vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(10)});
            wtx[sfDestination] = dest.human();
            return wtx;
        };

        // Without any preauth, withdraw to dest fails
        env(withdrawToDest(), Ter{tecNO_PERMISSION});
        env.close();

        // Issue and accept a credential for the depositor (with expiration)
        auto jv = credentials::create(depositor, credIssuer, credType);
        std::uint32_t const expiration =
            env.current()->header().parentCloseTime.time_since_epoch().count() + 100;
        jv[sfExpiration.jsonName] = expiration;
        env(jv);
        env(credentials::accept(depositor, credIssuer, credType));
        env.close();

        auto const credKeylet = credentials::keylet(depositor, credIssuer, credType);
        auto const credIdx =
            credentials::ledgerEntry(env, depositor, credIssuer, credType)[jss::result][jss::index]
                .asString();

        // dest authorizes deposits from holders of credentials issued by credIssuer
        env(deposit::authCredentials(dest, {{.issuer = credIssuer, .credType = credType}}));
        env.close();

        // Withdraw without supplying credentials still fails
        env(withdrawToDest(), Ter{tecNO_PERMISSION});
        env.close();

        if (!fixEnabled)
        {
            // Pre-fix: sfCredentialIDs in VaultWithdraw is rejected as disabled
            env(withdrawToDest(), credentials::Ids({credIdx}), Ter{temDISABLED});
            env.close();
            return;
        }

        // Withdraw with credentials succeeds
        env(withdrawToDest(), credentials::Ids({credIdx}));
        env.close();

        // Bad credential id is rejected
        std::string const invalidIdx =
            "0E0B04ED60588A758B67E21FBBE95AC5A63598BA951761DC0EC9C08D7E01E034";
        env(withdrawToDest(), credentials::Ids({invalidIdx}), Ter{tecBAD_CREDENTIALS});
        env.close();

        // Malformed credential array (duplicates) is rejected by checkFields
        env(withdrawToDest(), credentials::Ids({credIdx, credIdx}), Ter{temMALFORMED});
        env.close();

        // Valid credential not authorized by dest hits authorizedDepositPreauth error path
        char const credType2[] = "fghij";
        env(credentials::create(depositor, credIssuer, credType2));
        env(credentials::accept(depositor, credIssuer, credType2));
        env.close();
        auto const credIdx2 =
            credentials::ledgerEntry(env, depositor, credIssuer, credType2)[jss::result][jss::index]
                .asString();
        env(withdrawToDest(), credentials::Ids({credIdx2}), Ter{tecNO_PERMISSION});
        env.close();

        // Advance time past expiration: credentials yield tecEXPIRED and are deleted
        env.close(150s);
        BEAST_EXPECT(env.le(credKeylet));
        env(withdrawToDest(), credentials::Ids({credIdx}), Ter{tecEXPIRED});
        env.close();
        BEAST_EXPECT(!env.le(credKeylet));
    }

public:
    void
    run() override
    {
        testWithDomainCheck();
        testDomainLossAfterAcquisition();
        testDomainCheckBuyerSideOffer();
        testWithDomainChecXRP();
        testVaultWithdrawPrivateDestinationDomain(all_ - fixCleanup3_4_0);
        testVaultWithdrawPrivateDestinationDomain(all_);
        testWithdrawCredentialDepositPreauth(all_ - fixCleanup3_4_0);
        testWithdrawCredentialDepositPreauth(all_);
    }
};

BEAST_DEFINE_TESTSUITE(VaultDomain, app, xrpl);

}  // namespace xrpl
