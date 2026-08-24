#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsVault_test : public InvariantsBase
{
    void
    testVault()  // NOLINT(readability-function-size)
    {
        using namespace test::jtx;

        struct AccountAmount
        {
            AccountID account;
            int amount;
        };
        struct Adjustments
        {
            // NOLINTBEGIN(readability-redundant-member-init)
            std::optional<int> assetsTotal = std::nullopt;
            std::optional<int> assetsAvailable = std::nullopt;
            std::optional<int> lossUnrealized = std::nullopt;
            std::optional<int> assetsMaximum = std::nullopt;
            std::optional<int> sharesTotal = std::nullopt;
            std::optional<int> vaultAssets = std::nullopt;
            std::optional<AccountAmount> accountAssets = std::nullopt;
            std::optional<AccountAmount> accountShares = std::nullopt;
            // NOLINTEND(readability-redundant-member-init)
        };
        constexpr auto kAdjust = [&](ApplyView& ac, xrpl::Keylet keylet, Adjustments args) {
            auto sleVault = ac.peek(keylet);
            if (!sleVault)
                return false;

            auto const mptIssuanceID = (*sleVault)[sfShareMPTID];
            auto sleShares = ac.peek(keylet::mptokenIssuance(mptIssuanceID));
            if (!sleShares)
                return false;

            // These two fields are adjusted in absolute terms
            if (args.lossUnrealized)
                (*sleVault)[sfLossUnrealized] = *args.lossUnrealized;
            if (args.assetsMaximum)
                (*sleVault)[sfAssetsMaximum] = *args.assetsMaximum;

            // Remaining fields are adjusted in terms of difference
            if (args.assetsTotal)
                (*sleVault)[sfAssetsTotal] = *(*sleVault)[sfAssetsTotal] + *args.assetsTotal;
            if (args.assetsAvailable)
            {
                (*sleVault)[sfAssetsAvailable] =
                    *(*sleVault)[sfAssetsAvailable] + *args.assetsAvailable;
            }
            ac.update(sleVault);

            if (args.sharesTotal)
            {
                (*sleShares)[sfOutstandingAmount] =
                    *(*sleShares)[sfOutstandingAmount] + *args.sharesTotal;
                ac.update(sleShares);
            }

            auto const assets = *(*sleVault)[sfAsset];
            auto const pseudoId = *(*sleVault)[sfAccount];
            if (args.vaultAssets)
            {
                if (assets.native())
                {
                    auto slePseudoAccount = ac.peek(keylet::account(pseudoId));
                    if (!slePseudoAccount)
                        return false;
                    (*slePseudoAccount)[sfBalance] =
                        *(*slePseudoAccount)[sfBalance] + *args.vaultAssets;
                    ac.update(slePseudoAccount);
                }
                else if (assets.holds<MPTIssue>())
                {
                    auto const mptId = assets.get<MPTIssue>().getMptID();
                    auto sleMPToken = ac.peek(keylet::mptoken(mptId, pseudoId));
                    if (!sleMPToken)
                        return false;
                    (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + *args.vaultAssets;
                    ac.update(sleMPToken);
                }
                else
                {
                    return false;  // Not supporting testing with IOU
                }
            }

            if (args.accountAssets)
            {
                auto const& pair = *args.accountAssets;
                if (assets.native())
                {
                    auto sleAccount = ac.peek(keylet::account(pair.account));
                    if (!sleAccount)
                        return false;
                    (*sleAccount)[sfBalance] = *(*sleAccount)[sfBalance] + pair.amount;
                    ac.update(sleAccount);
                }
                else if (assets.holds<MPTIssue>())
                {
                    auto const mptID = assets.get<MPTIssue>().getMptID();
                    auto sleMPToken = ac.peek(keylet::mptoken(mptID, pair.account));
                    if (!sleMPToken)
                        return false;
                    (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + pair.amount;
                    ac.update(sleMPToken);
                }
                else
                {
                    return false;  // Not supporting testing with IOU
                }
            }

            if (args.accountShares)
            {
                auto const& pair = *args.accountShares;
                auto sleMPToken = ac.peek(keylet::mptoken(mptIssuanceID, pair.account));
                if (!sleMPToken)
                    return false;
                (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + pair.amount;
                ac.update(sleMPToken);
            }
            return true;
        };

        static constexpr auto kArgs = [](AccountID id, int adjustment, auto fn) -> Adjustments {
            Adjustments sample = {
                .assetsTotal = adjustment,
                .assetsAvailable = adjustment,
                .lossUnrealized = 0,
                .sharesTotal = adjustment,
                .vaultAssets = adjustment,
                .accountAssets =  //
                AccountAmount{.account = id, .amount = -adjustment},
                .accountShares =  //
                AccountAmount{.account = id, .amount = adjustment}};
            fn(sample);
            return sample;
        };

        Account const a3{"A3"};
        Account const a4{"A4"};
        auto const precloseXrp = [&](Account const& a1, Account const& a2, Env& env) -> bool {
            env.fund(XRP(1000), a3, a4);
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
            env(tx);
            env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a3, .id = keylet.key, .amount = XRP(10)}));
            return true;
        };

        testcase << "Vault general checks";
        doInvariantCheck(
            {"vault deletion succeeded without deleting a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                sleVault->setAccountID(sfAccount, a1.id());
                ac.view().insert(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"vault deleted by a wrong transaction type",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                {
                    auto const keylet =
                        keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                {
                    auto const keylet =
                        keylet::vault(a2.id(), SeqProxy::rawSequence(ac.view().seq()));
                    auto sleVault = ac.view().peek(keylet);
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);
                }
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                {
                    auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                    env(tx);
                }
                {
                    auto [tx, _] = vault.create({.owner = a2, .asset = xrpIssue()});
                    env(tx);
                }
                return true;
            });

        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const insertVault = [&](Account const a) {
                    auto const vaultKeylet = keylet::vault(a.id(), SeqProxy::rawSequence(sequence));
                    auto sleVault = std::make_shared<SLE>(vaultKeylet);
                    auto const vaultPage = ac.view().dirInsert(
                        keylet::ownerDir(a.id()), sleVault->key(), describeOwnerDir(a.id()));
                    sleVault->setFieldU64(sfOwnerNode, *vaultPage);
                    sleVault->setAccountID(sfAccount, a.id());
                    ac.view().insert(sleVault);
                };
                insertVault(a1);
                insertVault(a2);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        doInvariantCheck(
            {"deleted vault must also delete shares",
             "deleted Vault without deleting its pseudo-account"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().erase(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"deleted vault must have no shares outstanding",
             "deleted vault must have no assets outstanding",
             "deleted vault must have no assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().erase(sleVault);
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                // Note, such an "orphaned" update of MPT issuance attached to a
                // vault is invalid; ttVAULT_SET must also update Vault object.
                sleShares->setFieldH256(sfDomainID, uint256(13));
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"updated vault must have shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsMaximum] = 200;
                ac.view().update(sleVault);

                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, _] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault operation succeeded without updating shares",
             "assets available must not be greater than assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsTotal] = 9;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
                return true;
            });

        doInvariantCheck(
            {"set must not change assets outstanding",
             "set must not change assets available",
             "set must not change shares outstanding",
             "set must not change vault balance",
             "assets available must not be negative",
             "assets available must not be greater than assets outstanding",
             "assets outstanding must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto slePseudoAccount = ac.view().peek(keylet::account(*(*sleVault)[sfAccount]));
                if (!slePseudoAccount)
                    return false;
                (*slePseudoAccount)[sfBalance] = *(*slePseudoAccount)[sfBalance] - 10;
                ac.view().update(slePseudoAccount);

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(a4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.assetsAvailable = (kDropsPerXrp * -100).value();
                                   sample.assetsTotal = (kDropsPerXrp * -200).value();
                                   sample.sharesTotal = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldIssue(sfAsset, STIssue{sfAsset, MPTIssue(MPTID(42))});
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setAccountID(sfAccount, a2.id());
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfShareMPTID] = MPTID(42);
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"vault transaction must not change loss unrealized",
             "set must not change assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.lossUnrealized = 13;
                                   sample.assetsTotal = 20;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"loss unrealized must not exceed the difference "
             "between assets outstanding and available",
             "vault transaction must not change loss unrealized"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 100, [&](Adjustments& sample) {
                                   sample.lossUnrealized = 13;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // A negative loss unrealized must trip the invariant. ttLOAN_MANAGE is
        // allowed to change loss unrealized, so it isolates this check from the
        // "must not change loss unrealized" invariant. Gated behind
        // fixCleanup3_4_0 (see below).
        doInvariantCheck(
            {"loss unrealized must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.lossUnrealized = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // Without fixCleanup3_4_0 the same state must NOT trip the invariant,
        // preserving pre-amendment behavior (no fork risk).
        doInvariantCheck(
            makeEnv(defaultAmendments() - fixCleanup3_4_0),
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.lossUnrealized = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) {}},
            {tesSUCCESS, tesSUCCESS},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"set assets outstanding must not exceed assets maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.assetsMaximum = 1;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"assets maximum must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {
                                   sample.assetsMaximum = -1;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"set must not change shares outstanding",
             "updated zero sized vault must have no assets outstanding",
             "updated zero sized vault must have no assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfOutstandingAmount] = 0;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfMaximumAmount] = 10;
                ac.view().update(sleShares);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments&) {}));

                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                (*sleShares)[sfOutstandingAmount] = kMaxMpTokenAmount + 1;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        testcase << "Vault create";
        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsTotal] = 9;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets available",
                "assets available must not be greater than assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsAvailable] = 9;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "loss unrealized must not exceed the difference between assets "
                "outstanding and available",
                "vault transaction must not change loss unrealized",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfLossUnrealized] = 1;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "created vault must be empty",
                "create operation must not have updated a vault",
                "invalid OutstandingAmount balance 0 9 0",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().update(sleVault);
                (*sleShares)[sfOutstandingAmount] = 9;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {
                "assets maximum must not be negative",
                "create operation must not have updated a vault",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfAssetsMaximum] = Number(-1);
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"create operation must not have updated a vault",
             "shares issuer and vault pseudo-account must be the same",
             "shares issuer must be a pseudo-account",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                auto sleShares = ac.view().peek(keylet::mptokenIssuance((*sleVault)[sfShareMPTID]));
                if (!sleShares)
                    return false;
                ac.view().update(sleVault);
                (*sleShares)[sfIssuer] = a1.id();
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
                env(tx);
                return true;
            });

        doInvariantCheck(
            {"vault created by a wrong transaction type", "account root created illegally"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // The code below will create a valid vault with (almost) all
                // the invariants holding. Except one: it is created by the
                // wrong transaction type.
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =                             //
                    ac.view().rules().enabled(featureSingleAssetVault)  //
                    ? 0                                                 //
                    : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                sleVault->at(sfAccount) = pseudoId;
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = a1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =                             //
                    ac.view().rules().enabled(featureSingleAssetVault)  //
                    ? 0                                                 //
                    : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                // sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                // Setting wrong vault key
                sleAccount->setFieldH256(sfVaultID, uint256(42));
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                // sleVault->at(sfAccount) = pseudoId;
                // Setting wrong pseudo account ID
                sleVault->at(sfAccount) = a2.id();
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = a1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same", "shares issuer must exist"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(sequence));
                auto sleVault = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(a1.id()), sleVault->key(), describeOwnerDir(a1.id()));
                sleVault->setFieldU64(sfOwnerNode, *vaultPage);

                auto const sharesMptId = makeMptID(sequence, a2.id());
                auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(a2.id()), sharesKeylet, describeOwnerDir(a2.id()));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                // Setting wrong pseudo account ID
                sleShares->at(sfIssuer) = AccountID(42);
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                sleVault->at(sfAccount) = a2.id();
                sleVault->at(sfFlags) = 0;
                sleVault->at(sfSequence) = sequence;
                sleVault->at(sfOwner) = a1.id();
                sleVault->at(sfAssetsTotal) = Number(0);
                sleVault->at(sfAssetsAvailable) = Number(0);
                sleVault->at(sfLossUnrealized) = Number(0);
                sleVault->at(sfShareMPTID) = sharesMptId;
                sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;

                ac.view().insert(sleVault);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        testcase << "Vault deposit";
        doInvariantCheck(
            {"deposit must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit assets outstanding must not exceed assets maximum"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 200, [&](Adjustments& sample) {
                                   sample.assetsMaximum = 1;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        // This really convoluted unit tests makes the zero balance on the
        // depositor, by sending them the same amount as the transaction fee.
        // The operation makes no sense, but the defensive check in
        // ValidVault::finalize is otherwise impossible to trigger.
        doInvariantCheck(
            {"deposit must increase vault balance", "deposit must change depositor balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(a4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return kAdjust(ac.view(), keylet, kArgs(a3.id(), -10, [&](Adjustments& sample) {
                                   sample.accountAssets->amount = -100;
                               }));
            },
            XRPAmount{100},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    tx[sfFee] = XRPAmount(100);
                    tx[sfAccount] = a3.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"deposit must increase vault balance",
             "deposit must decrease depositor balance",
             "deposit must change vault and depositor balance by equal amount",
             "deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops from A2 to A3 to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 10;
                ac.view().update(sleA3);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.vaultAssets = -20;
                                   sample.accountAssets->amount = 10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops from A3 to vault to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 10;
                ac.view().update(sleA3);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountAssets->amount = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change depositor shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit must increase depositor shares",
             "deposit must change depositor and vault shares by equal amount",
             "deposit must not change vault balance by more than deposited "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = -5;
                                   sample.sharesTotal = -10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(5); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.assetsTotal = 11;
                               }));
            },
            XRPAmount{2000},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx[sfDelegate] = a3.id();
                    tx[sfFee] = XRPAmount(2000);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 10, [&](Adjustments& sample) {
                                   sample.assetsTotal = 7;
                                   sample.assetsAvailable = 7;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        testcase << "Vault withdrawal";
        doInvariantCheck(
            {"withdrawal must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // Almost identical to the really convoluted test for deposit, where the
        // depositor spends only the transaction fee. In case of withdrawal,
        // this test is almost the same as normal withdrawal where the
        // sfDestination would have been A4, but has been omitted.
        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops to A4 to enforce total XRP balance
                auto sleA4 = ac.view().peek(keylet::account(a4.id()));
                if (!sleA4)
                    return false;
                (*sleA4)[sfBalance] = *(*sleA4)[sfBalance] + 10;
                ac.view().update(sleA4);

                return kAdjust(ac.view(), keylet, kArgs(a3.id(), -10, [&](Adjustments& sample) {
                                   sample.accountAssets->amount = -100;
                               }));
            },
            XRPAmount{100},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfFee] = XRPAmount(100);
                    tx[sfAccount] = a3.id();
                    // This commented out line causes the invariant violation.
                    // tx[sfDestination] = A4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {
                "withdrawal must change vault and destination balance by equal amount",
                "withdrawal must decrease vault balance",
                "withdrawal must increase destination balance",
                "withdrawal and assets outstanding must add up",
                "withdrawal and assets available must add up",
            },
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));

                // Move 10 drops from A2 to A3 to enforce total XRP balance
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 10;
                ac.view().update(sleA3);

                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.vaultAssets = 10;
                                   sample.accountAssets->amount = -20;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                if (!kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                 *sample.vaultAssets -= 5;
                             })))
                    return false;
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!sleA3)
                    return false;
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] + 5;
                ac.view().update(sleA3);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [&](STObject& tx) { tx.setAccountID(sfDestination, a3.id()); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change depositor shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = 5;
                                   sample.sharesTotal = 10;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal and assets outstanding must add up",
             "withdrawal and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.assetsTotal = -15;
                                   sample.assetsAvailable = -15;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        doInvariantCheck(
            {"withdrawal and assets outstanding must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                (*sleA3)[sfBalance] = *(*sleA3)[sfBalance] - 2000;
                ac.view().update(sleA3);

                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.assetsTotal = -7;
                               }));
            },
            XRPAmount{2000},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx[sfDelegate] = a3.id();
                    tx[sfFee] = XRPAmount(2000);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp,
            TxAccount::A2);

        auto const precloseMpt = [&](Account const& a1, Account const& a2, Env& env) -> bool {
            env.fund(XRP(1000), a3, a4);

            // Create MPT asset
            {
                json::Value jv;
                jv[sfAccount] = a3.human();
                jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
                jv[sfFlags] = tfMPTCanTransfer;
                env(jv);
                env.close();
            }

            auto const mptID = makeMptID(env.seq(a3) - 1, a3);
            Asset const asset = MPTIssue(mptID);
            // Authorize A1 A2 A4
            {
                json::Value jv;
                jv[sfAccount] = a1.human();
                jv[sfTransactionType] = jss::MPTokenAuthorize;
                jv[sfMPTokenIssuanceID] = to_string(mptID);
                env(jv);
                jv[sfAccount] = a2.human();
                env(jv);
                jv[sfAccount] = a4.human();
                env(jv);

                env.close();
            }
            // Send tokens to A1 A2 A4
            {
                env(pay(a3, a1, asset(1000)));
                env(pay(a3, a2, asset(1000)));
                env(pay(a3, a4, asset(1000)));
                env.close();
            }

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = a1, .asset = asset});
            env(tx);
            env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = asset(10)}));
            env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = asset(10)}));
            env(vault.deposit({.depositor = a4, .id = keylet.key, .amount = asset(10)}));
            return true;
        };

        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = 5;
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [&](STObject& tx) { tx[sfAccount] = a3.id(); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt,
            TxAccount::A2);

        testcase << "Vault clawback";
        doInvariantCheck(
            {"clawback must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), -1, [&](Adjustments& sample) {
                                   sample.vaultAssets.reset();
                               }));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [&](STObject& tx) { tx[sfAccount] = a3.id(); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        // Not the same as below check: attempt to clawback XRP
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // Not the same as above check: attempt to clawback MPT by bad account
        doInvariantCheck(
            {"clawback may only be performed by the asset issuer"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a2.id(), 0, [&](Adjustments& sample) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_CLAWBACK, [&](STObject& tx) { tx[sfAccount] = a4.id(); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must decrease vault balance",
             "clawback must decrease holder shares",
             "clawback must change vault shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a4.id(), 10, [&](Adjustments& sample) {
                                   sample.sharesTotal = 0;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = a3.id();
                    tx[sfHolder] = a4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder shares"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a4.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares.reset();
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = a3.id();
                    tx[sfHolder] = a4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        doInvariantCheck(
            {"clawback must change holder and vault shares by equal amount",
             "clawback and assets outstanding must add up",
             "clawback and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet =
                    keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq() - 2));
                return kAdjust(ac.view(), keylet, kArgs(a4.id(), -10, [&](Adjustments& sample) {
                                   sample.accountShares->amount = -8;
                                   sample.assetsTotal = -7;
                                   sample.assetsAvailable = -7;
                               }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_CLAWBACK,
                [&](STObject& tx) {
                    tx[sfAccount] = a3.id();
                    tx[sfHolder] = a4.id();
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseMpt);

        // ─────────────────────────────────────────────────────────────
        // Closed-ended vault invariants added in ValidVault::finalize (create must supply both
        // dates and satisfy the redemption-buffer gap), deposit only in Subscription / NoPhase,
        // withdraw not in Investment, loan origination only in Investment.

        using d = NetClock::duration;
        using tp = NetClock::time_point;

        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);

        // Vault keylet captured by precloseClosedEnded so precheck does not have to rederive it
        // from ac.view().seq(), which depends on how many env.close() calls preclose issued.
        Keylet closedEndedKeylet = keylet::amendments();

        // Preclose that creates a closed-ended vault (in Subscription), optionally seeds it with
        // three deposits (so a1/a2/a3 hold a share MPToken that kAdjust can then adjust), and
        // optionally advances parent close time past SubscriptionDate. A negative @p advanceBySub
        // leaves the vault in Subscription.
        auto const precloseClosedEnded = [&](std::int32_t advanceBySub, bool doDeposit) {
            return [&, advanceBySub, doDeposit](
                       Account const& a1, Account const& a2, Env& env) -> bool {
                env.fund(XRP(1000), a3, a4);
                auto const sub = env.now().time_since_epoch().count() + 60;
                auto const red = sub + kMinInvestmentPeriod + 1'000'000;
                Vault const vault{env};
                auto [tx, keylet] = vault.create(
                    {.owner = a1,
                     .asset = xrpIssue(),
                     .vaultKind = closedEnded,
                     .subscriptionDate = sub,
                     .redemptionDate = red});
                env(tx);
                closedEndedKeylet = keylet;
                if (doDeposit)
                {
                    env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
                    env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = XRP(10)}));
                    env(vault.deposit({.depositor = a3, .id = keylet.key, .amount = XRP(10)}));
                }
                if (advanceBySub >= 0)
                    env.close(tp{d{sub + advanceBySub}});
                return true;
            };
        };

        // Manually insert a bare closed-ended vault (+ pseudo-account + share MPTokenIssuance)
        // directly into the view, bypassing the transactor path. Used to synthesize ttVAULT_CREATE
        // states no legitimate transactor would produce.
        auto const insertBareClosedEndedVault =
            [closedEnded](
                ApplyContext& ac,
                Account const& owner,
                std::optional<std::uint32_t> subscriptionDate,
                std::optional<std::uint32_t> redemptionDate) -> bool {
            auto const sequence = ac.view().seq();
            auto const vaultKeylet = keylet::vault(owner.id(), SeqProxy::rawSequence(sequence));
            auto sleVault = std::make_shared<SLE>(vaultKeylet);
            auto const vaultPage = ac.view().dirInsert(
                keylet::ownerDir(owner.id()), sleVault->key(), describeOwnerDir(owner.id()));
            if (!vaultPage)
                return false;
            sleVault->setFieldU64(sfOwnerNode, *vaultPage);

            auto const pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
            auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
            sleAccount->setAccountID(sfAccount, pseudoId);
            sleAccount->setFieldAmount(sfBalance, STAmount{});
            sleAccount->setFieldU32(sfSequence, 0);
            sleAccount->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
            sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
            ac.view().insert(sleAccount);

            auto const sharesMptId = makeMptID(sequence, pseudoId);
            auto const sharesKeylet = keylet::mptokenIssuance(sharesMptId);
            auto sleShares = std::make_shared<SLE>(sharesKeylet);
            auto const sharesPage = ac.view().dirInsert(
                keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
            if (!sharesPage)
                return false;
            sleShares->setFieldU64(sfOwnerNode, *sharesPage);
            sleShares->at(sfFlags) = 0;
            sleShares->at(sfIssuer) = pseudoId;
            sleShares->at(sfOutstandingAmount) = 0;
            sleShares->at(sfSequence) = sequence;

            sleVault->at(sfAccount) = pseudoId;
            sleVault->at(sfFlags) = 0;
            sleVault->at(sfSequence) = sequence;
            sleVault->at(sfOwner) = owner.id();
            sleVault->setFieldIssue(sfAsset, STIssue{sfAsset, Asset{xrpIssue()}});
            sleVault->at(sfAssetsTotal) = Number(0);
            sleVault->at(sfAssetsAvailable) = Number(0);
            sleVault->at(sfLossUnrealized) = Number(0);
            sleVault->at(sfShareMPTID) = sharesMptId;
            sleVault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;
            sleVault->at(sfVaultKind) = closedEnded;
            if (subscriptionDate)
                sleVault->at(sfSubscriptionDate) = *subscriptionDate;
            if (redemptionDate)
                sleVault->at(sfRedemptionDate) = *redemptionDate;

            ac.view().insert(sleVault);
            ac.view().insert(sleShares);
            return true;
        };

        testcase << "Vault create closed-ended";

        // A fresh closed-ended vault must carry both SubscriptionDate and RedemptionDate.
        doInvariantCheck(
            {"closed-ended vault must have SubscriptionDate and RedemptionDate"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return insertBareClosedEndedVault(ac, a1, std::nullopt, std::nullopt);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        // Gap smaller than MIN_INVESTMENT_PERIOD but with RedemptionDate > SubscriptionDate;
        // exercises the sub-minimum branch of the gap check.
        doInvariantCheck(
            {"closed-ended vault RedemptionDate - SubscriptionDate must be "
             "within [MIN_INVESTMENT_PERIOD, MAX_INVESTMENT_PERIOD)"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                std::uint32_t const sub = 1'000'000'000;
                std::uint32_t const red = sub + kMinInvestmentPeriod - 1;
                return insertBareClosedEndedVault(ac, a1, sub, red);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        // RedemptionDate strictly before SubscriptionDate; the signed int64 gap is negative and
        // is caught by the sub-minimum branch of the gap check.
        doInvariantCheck(
            {"closed-ended vault RedemptionDate - SubscriptionDate must be "
             "within [MIN_INVESTMENT_PERIOD, MAX_INVESTMENT_PERIOD)"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                std::uint32_t const sub = 1'000'000'000;
                std::uint32_t const red = sub - 1;
                return insertBareClosedEndedVault(ac, a1, sub, red);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        // Gap exactly MAX_INVESTMENT_PERIOD is out of range (bound is half-open on the right).
        doInvariantCheck(
            {"closed-ended vault RedemptionDate - SubscriptionDate must be "
             "within [MIN_INVESTMENT_PERIOD, MAX_INVESTMENT_PERIOD)"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                std::uint32_t const sub = 1'000'000'000;
                std::uint32_t const red = sub + kMaxInvestmentPeriod;
                return insertBareClosedEndedVault(ac, a1, sub, red);
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        testcase << "Vault deposit closed-ended";

        // A deposit into a closed-ended vault that has advanced past SubscriptionDate. kArgs
        // simulates an otherwise valid deposit shape so only the phase invariant fires.
        doInvariantCheck(
            {"deposit only allowed in Subscription or NoPhase"},
            [&](Account const&, Account const& a2, ApplyContext& ac) {
                return kAdjust(
                    ac.view(), closedEndedKeylet, kArgs(a2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseClosedEnded(/*advanceBySub=*/1, /*doDeposit=*/true),
            TxAccount::A2);

        testcase << "Vault withdrawal closed-ended";

        // A withdrawal from a closed-ended vault in the Investment phase.
        doInvariantCheck(
            {"withdrawal not allowed during Investment phase"},
            [&](Account const&, Account const& a2, ApplyContext& ac) {
                return kAdjust(
                    ac.view(), closedEndedKeylet, kArgs(a2.id(), -10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseClosedEnded(/*advanceBySub=*/1, /*doDeposit=*/true),
            TxAccount::A2);

        testcase << "Vault loan set";

        // ttLOAN_SET against a closed-ended vault that is not in Investment. finalizeLoanSet fires
        // on any vault mutation; touching the vault SLE with no field change is sufficient.
        doInvariantCheck(
            {"loan origination only allowed in Investment phase"},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sleVault = ac.view().peek(closedEndedKeylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseClosedEnded(/*advanceBySub=*/-1, /*doDeposit=*/false));

        testcase << "Vault loan set - closed-ended final payment past "
                    "RedemptionDate";

        // A newly-created loan against a closed-ended vault must satisfy StartDate +
        // PaymentInterval * PaymentRemaining < RedemptionDate. LoanSet::preclaim enforces the same
        // bound; this test synthesises an invalid loan directly in the ApplyView so the invariant
        // catches it even when preclaim is bypassed.
        Keylet closedEndedBrokerKeylet = keylet::amendments();
        std::uint32_t closedEndedRed = 0;
        doInvariantCheck(
            {"closed-ended loan final payment must precede RedemptionDate"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                // Touch the vault so ValidVault::finalizeLoanSet sees an
                // entry in afterVault_; the vault is in Investment, so
                // finalizeLoanSet itself passes.
                auto sleVault = ac.view().peek(closedEndedKeylet);
                if (!sleVault)
                    return false;
                ac.view().update(sleVault);

                // Read the broker's next loan sequence to build the loan
                // keylet the same way LoanSet::doApply would.
                auto sleBroker = ac.view().peek(closedEndedBrokerKeylet);
                if (!sleBroker)
                    return false;
                std::uint32_t const loanSeq = sleBroker->at(sfLoanSequence);

                // Synthesize a Loan whose final scheduled payment lands
                // exactly at RedemptionDate: StartDate = red, interval = 60,
                // remaining = 1 => red + 60 >= red.
                auto sleLoan = std::make_shared<SLE>(
                    keylet::loan(closedEndedBrokerKeylet.key, SeqProxy::rawSequence(loanSeq)));
                sleLoan->at(sfLoanBrokerID) = closedEndedBrokerKeylet.key;
                sleLoan->at(sfLoanSequence) = loanSeq;
                sleLoan->at(sfBorrower) = a1.id();
                sleLoan->at(sfStartDate) = closedEndedRed;
                sleLoan->at(sfPaymentInterval) = 60;
                sleLoan->at(sfPaymentRemaining) = 1;
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                ac.view().insert(sleLoan);
                return true;
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& a1, Account const&, Env& env) -> bool {
                auto const sub = env.now().time_since_epoch().count() + 60;
                auto const red = sub + kMinInvestmentPeriod + 1'000'000;
                closedEndedRed = red;

                Vault const vault{env};
                auto [tx, keylet] = vault.create(
                    {.owner = a1,
                     .asset = xrpIssue(),
                     .vaultKind = closedEnded,
                     .subscriptionDate = sub,
                     .redemptionDate = red});
                env(tx);
                closedEndedKeylet = keylet;

                // Create the loan broker; LoanBrokerSet has no phase gate.
                closedEndedBrokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
                env(loan_broker::set(a1, keylet.key));

                // Advance parent close time into Investment so
                // ValidVault::finalizeLoanSet is satisfied.
                env.close(tp{d{sub + 1}});
                return true;
            });
    }

    void
    testVaultComputeCoarsestScale()
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        PrettyAsset const vaultAsset = issuer["IOU"];

        struct TestCase
        {
            std::string name;
            std::int32_t expectedMinScale;
            std::vector<ValidVault::DeltaInfo> values;
        };

        for (auto const mantissaScale : MantissaRange::getAllScales())
        {
            if (mantissaScale == MantissaRange::MantissaScale::Small)
                continue;
            NumberMantissaScaleGuard const g{mantissaScale};

            auto makeDelta = [&vaultAsset](Number const& n) -> ValidVault::DeltaInfo {
                return {.delta = n, .scale = scale(n, vaultAsset.raw())};
            };

            auto const testCases = std::vector<TestCase>{
                {
                    .name = "No values",
                    .expectedMinScale = 0,
                    .values = {},
                },
                {
                    .name = "Mixed integer and Number values",
                    .expectedMinScale = -15,
                    .values = {makeDelta(1), makeDelta(-1), makeDelta(Number{10, -1})},
                },
                {
                    .name = "Mixed scales",
                    .expectedMinScale = -17,
                    .values =
                        {makeDelta(Number{1, -2}),
                         makeDelta(Number{5, -3}),
                         makeDelta(Number{3, -2})},
                },
                {
                    .name = "Equal scales",
                    .expectedMinScale = -16,
                    .values =
                        {makeDelta(Number{1, -1}),
                         makeDelta(Number{5, -1}),
                         makeDelta(Number{1, -1})},
                },
                {
                    .name = "Mixed mantissa sizes",
                    .expectedMinScale = -12,
                    .values =
                        {makeDelta(Number{1}),
                         makeDelta(Number{1234, -3}),
                         makeDelta(Number{12345, -6}),
                         makeDelta(Number{123, 1})},
                },
            };

            for (auto const& tc : testCases)
            {
                testcase("vault computeCoarsestScale: " + tc.name);

                auto const actualScale = ValidVault::computeCoarsestScale(tc.values);

                BEAST_EXPECTS(
                    actualScale == tc.expectedMinScale,
                    "expected: " + std::to_string(tc.expectedMinScale) +
                        ", actual: " + std::to_string(actualScale));
                for (auto const& num : tc.values)
                {
                    // None of these scales are far enough apart that rounding the
                    // values would lose information, so check that the rounded
                    // value matches the original.
                    auto const actualRounded = roundToAsset(vaultAsset, num.delta, actualScale);
                    BEAST_EXPECTS(
                        actualRounded == num.delta,
                        "number " + to_string(num.delta) + " rounded to scale " +
                            std::to_string(actualScale) + " is " + to_string(actualRounded));
                }
            }

            auto const testCases2 = std::vector<TestCase>{
                {
                    .name = "False equivalence",
                    .expectedMinScale = -15,
                    .values =
                        {
                            makeDelta(Number{1234567890123456789, -18}),
                            makeDelta(Number{12345, -4}),
                            makeDelta(Number{1}),
                        },
                },
            };

            // Unlike the first set of test cases, the values in these test could
            // look equivalent if using the wrong scale.
            for (auto const& tc : testCases2)
            {
                testcase("vault computeCoarsestScale: " + tc.name);

                auto const actualScale = ValidVault::computeCoarsestScale(tc.values);

                BEAST_EXPECTS(
                    actualScale == tc.expectedMinScale,
                    "expected: " + std::to_string(tc.expectedMinScale) +
                        ", actual: " + std::to_string(actualScale));
                std::optional<Number> first;
                Number firstRounded;
                for (auto const& num : tc.values)
                {
                    if (!first)
                    {
                        first = num.delta;
                        firstRounded = roundToAsset(vaultAsset, num.delta, actualScale);
                        continue;
                    }
                    auto const numRounded = roundToAsset(vaultAsset, num.delta, actualScale);
                    BEAST_EXPECTS(
                        numRounded != firstRounded,
                        "at a scale of " + std::to_string(actualScale) + " " +
                            to_string(num.delta) + " == " + to_string(*first));
                }
            }
        }
    }

    void
    run() override
    {
        testVault();
        testVaultComputeCoarsestScale();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsVault, app, xrpl);

}  // namespace xrpl::test
