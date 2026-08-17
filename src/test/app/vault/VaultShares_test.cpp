#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/rate.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultShares_test : public VaultTestBase
{
private:
    void
    testNonTransferableShares()
    {
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(1000), issuer, owner, depositor);
        env.close();

        Vault const vault{env};
        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(100)));
        env.trust(asset(1000), depositor);
        env(pay(issuer, depositor, asset(100)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        tx[sfFlags] = tfVaultShareNonTransferable;
        env(tx);
        env.close();

        {
            testcase("nontransferable deposits");
            auto tx1 =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(40)});
            env(tx1);

            auto tx2 = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(60)});
            env(tx2);
            env.close();
        }

        auto const vaultAccount =  //
            [&env, key = keylet.key, this]() -> AccountID {
            auto jvVault = env.rpc("vault_info", strHex(key));

            BEAST_EXPECT(jvVault[jss::result][jss::vault][sfAssetsTotal] == "100");
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][jss::shares][sfOutstandingAmount] == "100000000");

            // Vault pseudo-account
            return parseBase58<AccountID>(jvVault[jss::result][jss::vault][jss::Account].asString())
                .value();
        }();

        auto const mptId = makeMptID(1, vaultAccount);
        Asset const shares = mptId;

        {
            testcase("nontransferable shares cannot be moved");
            env(pay(owner, depositor, shares(10)), Ter{tecNO_AUTH});
            env(pay(depositor, owner, shares(10)), Ter{tecNO_AUTH});
        }

        {
            testcase("nontransferable shares can be used to withdraw");
            auto tx1 =
                vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(20)});
            env(tx1);

            auto tx2 = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(30)});
            env(tx2);
            env.close();
        }

        {
            testcase("nontransferable shares balance check");
            auto jvVault = env.rpc("vault_info", strHex(keylet.key));
            BEAST_EXPECT(jvVault[jss::result][jss::vault][sfAssetsTotal] == "50");
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][jss::shares][sfOutstandingAmount] == "50000000");
        }

        {
            testcase("nontransferable shares withdraw rest");
            auto tx1 =
                vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(20)});
            env(tx1);

            auto tx2 = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(30)});
            env(tx2);
            env.close();
        }

        {
            testcase("nontransferable shares delete empty vault");
            auto tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx);
            BEAST_EXPECT(!env.le(keylet));
        }
    }

    void
    testFailedPseudoAccount()
    {
        using namespace test::jtx;

        testcase("fail pseudo-account allocation");
        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Vault const vault{env};
        env.fund(XRP(1000), owner);

        auto const keylet = keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        for (int i = 0; i < 256; ++i)
        {
            AccountID const accountId = xrpl::pseudoAccountAddress(*env.current(), keylet.key);

            env(pay(env.master.id(), accountId, XRP(1000)),
                Seq(kAutofill),
                Fee(kAutofill),
                Sig(kAutofill));
        }

        auto [tx, keylet1] = vault.create({.owner = owner, .asset = xrpIssue()});
        BEAST_EXPECT(keylet.key == keylet1.key);
        env(tx, Ter{terADDRESS_COLLISION});
    }

    void
    testRemoveEmptyHoldingLockedAmount()
    {
        testcase("removeEmptyHolding deletes MPToken with sfLockedAmount");
        using namespace test::jtx;
        using namespace std::literals;

        auto const amendments = testableAmendments();
        auto runTest = [&](FeatureBitset f) {
            Env env{*this, f};
            auto const baseFee = env.current()->fees().base;

            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const bob{"bob"};

            env.fund(XRP(100000), issuer, owner, depositor, bob);
            env.close();

            Vault const vault{env};

            // Create an MPT asset for the vault
            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1000)));
            env.close();

            // Create vault
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const vaultSle = env.le(keylet);
            BEAST_EXPECT(vaultSle != nullptr);
            auto const shareMptID = vaultSle->at(sfShareMPTID);
            MPTIssue const shareIssue{shareMptID};

            // Depositor deposits 1000 asset units into vault, receiving shares
            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1000)}));
            env.close();

            // Check depositor has shares
            {
                auto const sleMpt = env.le(keylet::mptoken(shareMptID, depositor));
                BEAST_EXPECT(sleMpt != nullptr);
                BEAST_EXPECT(sleMpt->at(sfMPTAmount) == 1000);
            }

            // Escrow 500 of those shares
            env(escrow::create(depositor, bob, STAmount{shareIssue, 500}),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            // Verify: sfMPTAmount=500, sfLockedAmount=500
            {
                auto const sleMpt = env.le(keylet::mptoken(shareMptID, depositor));
                BEAST_EXPECT(sleMpt != nullptr);
                BEAST_EXPECT(sleMpt->at(sfLockedAmount) == 500);
                BEAST_EXPECT(sleMpt->at(sfMPTAmount) == 500);
            }

            // Withdraw remaining spendable shares — triggers removeEmptyHolding
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(500)}),
                Ter(tesSUCCESS));
            env.close();

            auto const sleMptAfter = env.le(keylet::mptoken(shareMptID, depositor));
            if (!f[fixCleanup3_1_3])
            {
                // Without the fix, removeEmptyHolding deletes the MPToken
                // even though sfLockedAmount > 0, leaving the escrow's locked
                // amount untracked.
                BEAST_EXPECT(sleMptAfter == nullptr);
            }
            else
            {
                // With the fix, MPToken must still exist with sfLockedAmount > 0
                // and sfMPTAmount == 0 (all spendable shares withdrawn).
                BEAST_EXPECT(sleMptAfter != nullptr);
                if (sleMptAfter)
                {
                    BEAST_EXPECT(sleMptAfter->at(sfLockedAmount) == 500);
                    BEAST_EXPECT(sleMptAfter->at(sfMPTAmount) == 0);
                }
            }
        };

        runTest(amendments - fixCleanup3_1_3);
        runTest(amendments);
    }

    void
    testRemoveEmptyHoldingConfidentialBalances()
    {
        testcase("removeEmptyHolding keeps MPToken with confidential balances");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};

        Account const issuer{"issuer"};
        Account const holder{"holder"};
        MPTTester mpt{env, issuer, {.holders = {holder}}};
        mpt.create({.authorize = MPTCreate::allHolders});

        auto const tokenKeylet = keylet::mptoken(mpt.issuanceID(), holder.id());
        auto const encryptedBalanceFields = {
            &sfConfidentialBalanceInbox,
            &sfConfidentialBalanceSpending,
            &sfIssuerEncryptedBalance,
            &sfAuditorEncryptedBalance};

        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            for (auto const field : encryptedBalanceFields)
            {
                Sandbox sb(&view, TapNone);
                auto const token = sb.peek(tokenKeylet);
                if (!BEAST_EXPECT(token))
                    return false;

                token->setFieldVL(*field, gMakeZeroBuffer(kEcGamalEncryptedTotalLength));
                sb.update(token);

                auto const dummyTx = *env.jt(noop(holder)).stx;
                BEAST_EXPECT(
                    removeEmptyHolding({sb, dummyTx}, holder.id(), MPTIssue(mpt.issuanceID()), j) ==
                    tecHAS_OBLIGATIONS);
                BEAST_EXPECT(sb.peek(tokenKeylet) != nullptr);
            }
            return true;
        });
    }

    void
    testReferenceHolding()
    {
        using namespace test::jtx;

        auto readReferenceHolding = [&](Env const& env,
                                        Keylet const& vaultKeylet) -> std::optional<uint256> {
            auto const sleVault = env.le(vaultKeylet);
            if (!sleVault)
                return std::nullopt;
            auto const sleIssuance = env.le(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
            if (!sleIssuance || !sleIssuance->isFieldPresent(sfReferenceHolding))
                return std::nullopt;
            return sleIssuance->getFieldH256(sfReferenceHolding);
        };

        // Post-fixCleanup3_2_0: vault share carries sfReferenceHolding
        // pointing to the vault pseudo's MPToken (for MPT-backed vaults)
        // or RippleState (for IOU-backed vaults).
        {
            testcase("sfReferenceHolding: MPT-backed vault, post-amendment");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            auto const pseudoId = sleVault->at(sfAccount);
            auto const expected = keylet::mptoken(mptt.issuanceID(), pseudoId).key;

            auto const stored = readReferenceHolding(env, keylet);
            BEAST_EXPECT(stored.has_value());
            BEAST_EXPECT(stored && *stored == expected);
            // The pointed-to MPToken must actually exist.
            BEAST_EXPECT(env.le(keylet::mptoken(mptt.issuanceID(), pseudoId)) != nullptr);
        }

        {
            testcase("sfReferenceHolding: IOU-backed vault, post-amendment");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1'000'000), owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            auto const pseudoId = sleVault->at(sfAccount);
            auto const expected = keylet::trustLine(pseudoId, asset.raw().get<Issue>()).key;

            auto const stored = readReferenceHolding(env, keylet);
            BEAST_EXPECT(stored.has_value());
            BEAST_EXPECT(stored && *stored == expected);
            // The pointed-to RippleState must actually exist.
            BEAST_EXPECT(env.le(keylet::trustLine(pseudoId, asset.raw().get<Issue>())) != nullptr);
        }

        // XRP-backed vaults leave the field absent: XRP has no separate
        // holding ledger entry and no transferability concept to inherit.
        {
            testcase("sfReferenceHolding: XRP-backed vault, field absent");
            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            env.fund(XRP(10'000), owner);
            env.close();

            PrettyAsset const asset{xrpIssue(), 1'000'000};
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(!readReferenceHolding(env, keylet).has_value());
        }

        // Pre-fixCleanup3_2_0: vault share has the field absent regardless
        // of underlying type.
        {
            testcase("sfReferenceHolding: vault share, pre-amendment");
            Env env{*this, testableAmendments() - fixCleanup3_2_0};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(!readReferenceHolding(env, keylet).has_value());
        }

        // Plain MPTokenIssuanceCreate (not a vault share) must never
        // populate the field. Only the post-amendment case is
        // interesting; pre-amendment nothing writes the field at all.
        {
            testcase("sfReferenceHolding: plain MPT issuance never set");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            env.fund(XRP(10'000), issuer);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            env.close();

            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptt.issuanceID()));
            if (BEAST_EXPECT(sleIssuance))
                BEAST_EXPECT(!sleIssuance->isFieldPresent(sfReferenceHolding));
        }
    }

    // Probe every transactor surface that might delete the vault pseudo-
    // account's underlying holding (the MPToken or RippleState pointed to
    // by sfReferenceHolding). Each scenario asserts either that the
    // existing pseudo-account guards stop the deletion at preclaim, or
    // that the ledger leaves the holding intact afterwards. This is a
    // regression guard: if any of these guards regresses, the share's
    // sfReferenceHolding pointer would dangle and the new ValidMPTIssuance
    // invariant would catch it - but we want to fail much earlier, at
    // the transactor's preclaim / doApply, not at invariant time.
    void
    testHoldingDeletionBlocked()
    {
        using namespace test::jtx;

        // Helper: read the share's referenced holding and confirm the
        // pointed-to SLE still exists after the probe.
        auto referencedHoldingExists = [&](Env const& env, Keylet const& vaultKeylet) -> bool {
            auto const sleVault = env.le(vaultKeylet);
            if (!sleVault)
                return false;
            auto const sleIssuance = env.le(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
            if (!sleIssuance || !sleIssuance->isFieldPresent(sfReferenceHolding))
                return false;
            auto const holdingKey = sleIssuance->getFieldH256(sfReferenceHolding);
            return env.le(keylet::unchecked(holdingKey)) != nullptr;
        };

        // ---- MPT-backed vault ----------------------------------------
        {
            testcase("vault pseudo MPToken: Clawback blocked by tecPSEUDO_ACCOUNT");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(10'000), issuer, owner, depositor);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            Account const pseudoAccount{"vault-pseudo", env.le(keylet)->at(sfAccount)};
            // Issuer attempts to claw back the FULL underlying balance
            // (500) directly from the vault pseudo-account. With the
            // full amount, the doApply path would drain the pseudo's
            // MPToken to zero and removeEmptyHolding would erase it -
            // if doApply ever ran. SAV's pseudo-account guard at
            // Clawback.cpp:201 refuses at preclaim with
            // tecPSEUDO_ACCOUNT before any state change.
            env(claw(issuer, asset(500), pseudoAccount), Ter{tecPSEUDO_ACCOUNT});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
            // Sanity: pseudo's full balance is intact.
            BEAST_EXPECT(env.balance(pseudoAccount, asset).number() == 500);
        }

        {
            testcase("vault pseudo MPToken: Issuer cannot Unauthorize pseudo");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = issuer, .holder = owner});
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            auto const pseudoId = env.le(keylet)->at(sfAccount);
            // Issuer attempts MPTokenAuthorize against the pseudo with
            // tfMPTUnauthorize. MPTokenAuthorize.cpp blocks pseudo
            // accounts via isPseudoAccount; the pseudo's MPToken is
            // preserved. Construct the tx manually since the pseudo
            // lacks a signing key, and the issuer-driven flavour is
            // expressed via sfHolder.
            json::Value jv;
            jv[sfAccount] = issuer.human();
            jv[sfHolder] = toBase58(pseudoId);
            jv[sfMPTokenIssuanceID] = to_string(mptt.issuanceID());
            jv[sfFlags] = tfMPTUnauthorize;
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            env(jv, Ter{tecNO_PERMISSION});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
        }

        {
            testcase("vault pseudo MPToken: MPTokenIssuanceDestroy blocked while vault holds");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(10'000), issuer, owner, depositor);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            // While the vault holds outstanding underlying, the issuer
            // cannot destroy the issuance. tecHAS_OBLIGATIONS confirms
            // the protection - and as a side effect, the share's
            // sfReferenceHolding pointer cannot be left pointing at a
            // ghost issuance.
            mptt.destroy({.id = mptt.issuanceID(), .err = tecHAS_OBLIGATIONS});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
        }

        // ---- IOU-backed vault ----------------------------------------
        {
            testcase("vault pseudo trust line: Clawback blocked by tecPSEUDO_ACCOUNT");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1'000'000), owner);
            env(pay(issuer, owner, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            Account const pseudoAccount{"vault-pseudo", env.le(keylet)->at(sfAccount)};
            // Issuer attempts to claw back the FULL IOU balance (500)
            // directly from the vault pseudo. With the full amount, the
            // doApply path would drain the trust line to zero and (if
            // both reserve flags clear) trustDelete would erase it - if
            // doApply ever ran. The same SAV pseudo-account guard
            // refuses at preclaim with tecPSEUDO_ACCOUNT. The amount's
            // STAmount issuer field is the holder, per IOU clawback
            // convention.
            env(claw(issuer, pseudoAccount["IOU"](500)), Ter{tecPSEUDO_ACCOUNT});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
            // Sanity: pseudo's full balance is intact.
            BEAST_EXPECT(env.balance(pseudoAccount, asset).number() == 500);
        }

        {
            testcase("vault pseudo trust line: TrustSet limit=0 from issuer preserves line");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1'000'000), owner);
            env(pay(issuer, owner, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            // Issuer submits TrustSet with limit=0 against the vault
            // pseudo. The pseudo's side of the line still has the
            // original (non-zero) limit and a non-zero balance, so the
            // line is preserved - even though the issuer cleared its
            // own side. trustDelete only fires when both limits clear
            // and the balance is zero.
            Account const pseudoAccount{"vault-pseudo", env.le(keylet)->at(sfAccount)};
            env(trust(issuer, pseudoAccount["IOU"](0)));
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
        }

        // ---- Positive control: VaultDelete is the only legitimate path
        {
            testcase("vault pseudo holding: VaultDelete is the legitimate cleanup path");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));
            auto const pseudoId = env.le(keylet)->at(sfAccount);
            auto const sharedMptId = env.le(keylet)->at(sfShareMPTID);
            auto const holdingKeylet = keylet::mptoken(mptt.issuanceID(), pseudoId);

            // VaultDelete tears down the vault pseudo's holding, the
            // share issuance, and the pseudo-account itself. Invariant
            // permits this because the tx is ttVAULT_DELETE.
            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();

            BEAST_EXPECT(env.le(keylet) == nullptr);
            BEAST_EXPECT(env.le(holdingKeylet) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptokenIssuance(sharedMptId)) == nullptr);
        }
    }

public:
    void
    run() override
    {
        testNonTransferableShares();
        testFailedPseudoAccount();
        testRemoveEmptyHoldingLockedAmount();
        testRemoveEmptyHoldingConfidentialBalances();
        testReferenceHolding();
        testHoldingDeletionBlocked();
    }
};

BEAST_DEFINE_TESTSUITE(VaultShares, app, xrpl);

}  // namespace xrpl
