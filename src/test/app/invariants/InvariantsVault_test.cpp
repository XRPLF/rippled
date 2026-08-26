#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
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
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <array>
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
    FeatureBitset const all_{test::jtx::testableAmendments()};

    void
    testVault()  // NOLINT(readability-function-size)
    {
        using namespace test::jtx;

        struct AccountAmount
        {
            AccountID account;
            int amount;
        };
        // Parameters for a synthetic loan object created alongside a vault
        // adjustment. The interest due booked to the vault is
        // totalValueOutstanding - principalOutstanding - managementFeeOutstanding.
        struct LoanParams
        {
            int principalOutstanding = 0;
            int totalValueOutstanding = 0;
            int managementFeeOutstanding = 0;
            AccountID borrower = beast::kZero;
            // Real broker key the created loan should reference. If unset,
            // the vault key is used as a fallback for legacy callers.
            uint256 brokerKey = beast::kZero;
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
            std::optional<LoanParams> createLoan = std::nullopt;
            // Number of loan objects to create (only used when createLoan is
            // set); a valid loan set creates exactly one.
            int loanCount = 1;
            // NOLINTEND(readability-redundant-member-init)
        };
        constexpr auto kAdjust = [&](ApplyView& ac, xrpl::Keylet keylet, Adjustments args) {
            // Avoid uint64 + negative-int wrap (flagged by UBSan
            // unsigned-integer-overflow) when adjusting UINT64 fields.
            auto const addSigned = [](std::uint64_t current, int adj) -> std::uint64_t {
                return adj >= 0  //
                    ? current + static_cast<std::uint64_t>(adj)
                    : current - static_cast<std::uint64_t>(-adj);
            };
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
                    addSigned(*(*sleShares)[sfOutstandingAmount], *args.sharesTotal);
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
                    (*sleMPToken)[sfMPTAmount] =
                        addSigned(*(*sleMPToken)[sfMPTAmount], *args.vaultAssets);
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
                    (*sleMPToken)[sfMPTAmount] =
                        addSigned(*(*sleMPToken)[sfMPTAmount], pair.amount);
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
                (*sleMPToken)[sfMPTAmount] = addSigned(*(*sleMPToken)[sfMPTAmount], pair.amount);
                ac.update(sleMPToken);
            }

            if (args.createLoan)
            {
                auto const& lp = *args.createLoan;
                bool const anyOutstanding = lp.principalOutstanding != 0 ||
                    lp.totalValueOutstanding != 0 || lp.managementFeeOutstanding != 0;
                // If the caller supplied a broker key, use it; otherwise fall
                // back to the vault key so pre-existing callers keep working.
                uint256 const brokerKey = lp.brokerKey != beast::kZero ? lp.brokerKey : keylet.key;
                for (std::uint32_t seq = 1; seq <= static_cast<std::uint32_t>(args.loanCount);
                     ++seq)
                {
                    auto sleLoan = makeLoanSle(brokerKey, seq, lp.borrower);
                    sleLoan->at(sfPrincipalOutstanding) = Number(lp.principalOutstanding);
                    sleLoan->at(sfTotalValueOutstanding) = Number(lp.totalValueOutstanding);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(lp.managementFeeOutstanding);
                    sleLoan->setFieldU32(sfPaymentRemaining, anyOutstanding ? 1 : 0);
                    ac.insert(sleLoan);
                }
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

        // Under featureLendingProtocolV1_1 vault immutability is enforced by
        // NoModifiedUnmodifiableFields (class-1, both passes), which reports
        // "changed an unchangeable field" and escalates to tef on pass 2.
        doInvariantCheck(
            {"changed an unchangeable field"},
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
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"changed an unchangeable field"},
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
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        doInvariantCheck(
            {"changed an unchangeable field"},
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
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // Pre-featureLendingProtocolV1_1 the immutability of sfAsset, sfAccount
        // and sfShareMPTID is enforced by ValidVault directly, which reports
        // "violation of vault immutable data" on the first pass. ValidVault
        // returns early on the second pass (result already tec), so the check
        // does not escalate to tef. Once V1_1 activates, the same fields are
        // covered by NoModifiedUnmodifiableFields (see the three cases above);
        // the two paths are mutually exclusive so both need coverage.
        auto const preLendingV11Amendments = all_ - featureLendingProtocolV1_1;
        doInvariantCheck(
            makeEnv(preLendingV11Amendments),
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
            makeEnv(preLendingV11Amendments),
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
            makeEnv(preLendingV11Amendments),
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
        // preserving pre-amendment behavior (no fork risk). Also remove
        // featureLendingProtocolV1_1 so finalizeLoanManage's stricter checks
        // (exactly one loan touched) do not fire from a bare vault mutation
        // that does not touch a loan.
        doInvariantCheck(
            makeEnv(all_ - fixCleanup3_4_0 - featureLendingProtocolV1_1),
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

        testcase << "Vault loan operations";

        // ttLOAN_MANAGE (impair): assets outstanding must not change. Only
        // assets outstanding is bumped, so the common checks (vault balance,
        // assets available, shares) all pass and only the impair/unimpair
        // sub-check fires.
        doInvariantCheck(
            {"loan impair/unimpair must not change assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsTotal = 100});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE with none of the sub-operation flags (impair,
        // unimpair, default) is a no-op and must not modify the vault.
        doInvariantCheck(
            {"loan manage without a sub-operation must not modify the vault"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsTotal = 100});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: a valid loan is created, but the vault (pseudo-account)
        // balance does not change, so the vault balance check trips.
        doInvariantCheck(
            {"loan set must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: the balance decreases, but not by the principal requested
        doInvariantCheck(
            {"loan set must decrease vault balance by the principal requested"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100},
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{
                ttLOAN_SET,
                [](STObject& tx) {
                    tx.at(sfPrincipalRequested) = Number(200);
                    tx.makeFieldPresent(sfCounterpartySignature);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: vault balance decreases by the principal requested, but
        // assets available decreases by a different amount, so only the shared
        // "vault balance and assets available" check trips.
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -150,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{
                ttLOAN_SET,
                [](STObject& tx) {
                    tx.at(sfPrincipalRequested) = Number(200);
                    tx.makeFieldPresent(sfCounterpartySignature);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: principal matches, but no loan object is created
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200}});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        Keylet loanSetBrokerKeylet = keylet::amendments();
        Keylet loanSetVaultKeylet = keylet::amendments();
        auto const precloseXrpWithBroker =
            [&](Account const& a1, Account const& a2, Env& env) -> bool {
            env.fund(XRP(1000), a3, a4);
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            loanSetBrokerKeylet = createLoanBroker(a1, env, xrpAsset);
            auto const sleBroker = env.le(loanSetBrokerKeylet);
            if (!BEAST_EXPECT(sleBroker))
                return false;
            loanSetVaultKeylet = keylet::vault(sleBroker->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a1, .id = loanSetVaultKeylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a2, .id = loanSetVaultKeylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a3, .id = loanSetVaultKeylet.key, .amount = XRP(10)}));
            return true;
        };

        // ttLOAN_SET: principal matches, but more than one loan is created
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return kAdjust(
                    ac.view(),
                    loanSetVaultKeylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan =
                            LoanParams{
                                .principalOutstanding = 100,
                                .totalValueOutstanding = 100,
                                .borrower = a1.id(),
                                .brokerKey = loanSetBrokerKeylet.key,
                            },
                        .loanCount = 2});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            // ValidVault skips its transaction-specific checks when the
            // incoming result is already tec, so the second pass preserves it.
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrpWithBroker);

        // ttLOAN_SET: no new loan is created, but an existing loan is modified
        // instead. ValidLoan rejects this operation shape.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.fund(XRP(1000), a3);
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            auto const sleBroker = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBroker))
                return;
            auto const vaultKeylet = keylet::vault(sleBroker->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a1, .id = vaultKeylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a3, .id = vaultKeylet.key, .amount = XRP(10)}));
            env.close();

            OpenView ov{*env.current()};

            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            // Pre-existing loan in the base view; modifying it in the apply
            // view is what ValidLoan must reject for a set.
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_SET, [](STObject& t) { t.at(sfPrincipalRequested) = Number(200); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Move the vault balance and available assets to match the
            // principal requested, so the funding checks pass and only the
            // cardinality shape is left to trip.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200}})))
                return;
            // Modify the pre-existing loan to produce the operation shape that
            // a set transaction must never produce.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(50);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "lending transaction must not modify an existing loan"));
        }

        // ttLOAN_SET: principal matches, but shares outstanding changes
        doInvariantCheck(
            {"shares outstanding must only change by deposit, withdraw, or clawback"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .sharesTotal = 10,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .accountShares = AccountAmount{.account = a2.id(), .amount = 10},
                        .createLoan = LoanParams{
                            .principalOutstanding = 200,
                            .totalValueOutstanding = 200,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{
                ttLOAN_SET,
                [](STObject& tx) {
                    tx.at(sfPrincipalRequested) = Number(200);
                    tx.makeFieldPresent(sfCounterpartySignature);
                }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_SET: everything balances (principal released, exactly one loan
        // created booking zero interest, shares untouched, assets outstanding
        // unchanged), but the created loan records a principal outstanding that
        // differs from the principal requested. The vault only released 200 to
        // the borrower, yet the loan claims 300 principal, decoupling the
        // borrower's debt from the assets actually lent and enabling share-price
        // manipulation on repayment.
        doInvariantCheck(
            {"loan set principal outstanding must equal principal requested"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan = LoanParams{
                            .principalOutstanding = 300,
                            .totalValueOutstanding = 300,
                            .borrower = a1.id(),
                            .brokerKey = loanSetBrokerKeylet.key,
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrpWithBroker);

        // ttLOAN_SET pre-featureLendingProtocolV1_1: finalizeLoanSet short-
        // circuits and returns success without inspecting the loan or the
        // vault. The same state that trips the principal-outstanding check
        // under V1_1 must be silently accepted here.
        doInvariantCheck(
            makeEnv(all_ - featureLendingProtocolV1_1),
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -200,
                        .vaultAssets = -200,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 200},
                        .createLoan = LoanParams{
                            .principalOutstanding = 300,
                            .totalValueOutstanding = 300,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(200); }},
            {tesSUCCESS, tesSUCCESS},
            precloseXrp);

        // ttLOAN_SET: the loan's broker must record the newly-originated
        // debt. Vault cash flows and loan shape are correct; the broker is
        // touched (so the "modify exactly the loan's broker" cardinality
        // check passes) but its sfDebtTotal is left unchanged, so
        // `delta DebtTotal (0) != owedToVault (100)` and the residual check
        // trips. Requires a real broker on the ledger, so the setup is
        // bespoke.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            // Fund the vault so it can release the principal.
            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Move principal (100) out of the vault to the borrower a2 with
            // matching book-keeping so the earlier funding-side checks pass.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                            .brokerKey = brokerKeylet.key,
                        }})))
                return;

            // Touch the broker so before/afterBroker_ snapshots are captured
            // with matching keys, but leave sfDebtTotal at its base value.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set must increase broker debt total by the amount the "
                "new loan owes to the vault"));
        }

        // ttLOAN_SET: the borrower must receive the requested principal (net
        // of origination fee). Vault flows, DebtTotal and the loan shape all
        // balance, but the borrower is credited with only half the principal
        // and the remainder is routed to an unrelated account. The vault-side
        // identity still holds (assetsTotal/assetsAvailable/owedToVault
        // balance), so only the borrower-disburse check fires.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2, a3);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault releases 100 (correct principal); the borrower only sees
            // 50 of it, the other 50 is dropped on a3. The vault-side
            // identity depends only on the vault, so it still holds.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 50},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                            .brokerKey = brokerKeylet.key,
                        }})))
                return;

            // Absorb the remaining 50 on an unrelated account so the vault
            // ledger stays whole while the borrower is short-changed.
            {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!BEAST_EXPECT(sleA3))
                    return;
                sleA3->at(sfBalance) = *sleA3->at(sfBalance) + 50;
                ac.view().update(sleA3);
            }

            // Broker's DebtTotal must grow by owedToVault (100) so the
            // earlier DebtTotal check passes and the borrower-disburse
            // check is the one that trips.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) + Number(100);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set must credit the borrower with the principal net of "
                "origination fee"));
        }

        // ttLOAN_SET: when the loan carries a non-zero origination fee, the
        // broker owner must be credited with that fee. Vault flows, DebtTotal
        // and the borrower-disburse (principal net of fee) all balance, but
        // the fee is dropped on an unrelated account instead of the broker
        // owner, so only the broker-owner-disburse check fires.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2, a3);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault releases 100; borrower receives principal net of the
            // 10-unit origination fee, so a2 gets 90 (the correct amount).
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 90},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                            .brokerKey = brokerKeylet.key,
                        }})))
                return;

            // Route the origination fee to an unrelated account so the
            // broker owner (a1) delta stays at zero.
            {
                auto sleA3 = ac.view().peek(keylet::account(a3.id()));
                if (!BEAST_EXPECT(sleA3))
                    return;
                sleA3->at(sfBalance) = *sleA3->at(sfBalance) + 10;
                ac.view().update(sleA3);
            }

            // Stamp the origination fee so the borrower-net-of-fee expected
            // value is 90.
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfLoanOriginationFee) = Number(10);
                ac.view().update(sleLoan);
            }

            // owedToVault = 100 (totalValueOutstanding), so bump DebtTotal
            // by 100 to keep the earlier DebtTotal check happy.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) + Number(100);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set must credit the broker owner with the origination fee"));
        }

        // ttLOAN_SET: vault-side accounting identity
        // `delta AssetsTotal − delta AssetsAvailable == loan.owedToVault(version)`.
        // Vault balance and assets available fall by 100 (matching the
        // principal release) but assets total is bumped by an extra 10, so
        // the residual is 10 rather than zero. Every other check (funding,
        // borrower/broker owner disburse, DebtTotal, cardinality) passes.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            STTx const tx{ttLOAN_SET, [&](STObject& t) {
                              t.at(sfPrincipalRequested) = Number(100);
                              t.at(sfLoanBrokerID) = brokerKeylet.key;
                          }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault releases 100 (both vault balance and assetsAvailable
            // drop by 100), but assetsTotal is bumped by an extra 10 -
            // interest booking with no corresponding loan-side obligation.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsTotal = 10,
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a2.id(),
                            .brokerKey = brokerKeylet.key,
                        }})))
                return;

            // Bump DebtTotal by owedToVault (100) so the earlier DebtTotal
            // check passes.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) + Number(100);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan set assets outstanding must match the principal "
                "released and the amount the new loan owes to the vault"));
        }

        // ttLOAN_MANAGE: no loan is touched at all. Every lending transaction
        // must operate on exactly one loan; a manage with none is spurious.
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: a loan is created rather than modified. This object-
        // existence rule applies on both invariant passes.
        doInvariantCheck(
            {"Loan created by a transaction other than LoanSet"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: vault balance and assets available do not add up
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsAvailable = -100});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: loss unrealized driven negative
        doInvariantCheck(
            {"loss unrealized must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.lossUnrealized = -1});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE: shares outstanding changes
        //
        // ValidMPTPayment enforces its OutstandingAmount balance identity
        // regardless of TER under featureLendingProtocolV1_1, and the harness runs both
        // invariant passes against the same view (no reset in between), so
        // visitEntry accumulates the MPT delta on the second pass and trips
        // ValidMPTPayment alongside the share-change check -> escalation
        // to tef. Real production always resets between passes.
        doInvariantCheck(
            {"shares outstanding must only change by deposit, withdraw, or clawback"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .sharesTotal = 10,
                        .accountShares = AccountAmount{.account = a2.id(), .amount = 10}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (impair): assets available must not change
        doInvariantCheck(
            {"loan impair/unimpair must not change assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanImpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (default): assets available must not decrease
        doInvariantCheck(
            {"loan default must not decrease assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = -100,
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (default): assets outstanding must not increase
        doInvariantCheck(
            {"loan default must not increase assets outstanding"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 100,
                        .vaultAssets = 100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -100}});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (default): the object-level identity
        // `Delta Vault.AssetsAvailable == Delta Vault.pseudo-account balance`
        // must hold. Bump assets available by +50 while leaving the vault
        // pseudo-account (and the source of the first-loss capital) untouched:
        // the shared identity fires because the two ledgers no longer add up.
        // Sourced sibling: PR #7732 review discussion r3756829578.
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.assetsAvailable = 50});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (unimpair): loss unrealized must not increase. Bumping
        // loss unrealized upward is the wrong direction for unimpair, which
        // reverses a paper loss.
        doInvariantCheck(
            {"loan impair must not decrease, and loan unimpair must not "
             "increase, loss unrealized"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.lossUnrealized = 5});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanUnimpair); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_MANAGE (impair): loss unrealized must not decrease. The base
        // ledger cannot express a nonzero prior lossUnrealized through the
        // shared harness, so the setup is bespoke: seed the vault with a small
        // paper loss and then drop it back to zero under an impair — the
        // wrong direction for impair, which only ever grows the paper loss.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            // Seed the vault with a paper loss in the base view so a
            // decrease in the apply view registers as a negative delta. The
            // SLE is cloned so the base and apply views hold separate copies.
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfLossUnrealized) = Number(5);
                ov.rawReplace(sleVault);
            }

            STTx const tx{ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanImpair); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Reset lossUnrealized to zero under an impair: after (0) < before
            // (5), so the delta is negative and the sign check must reject.
            auto sleVault = ac.view().peek(vaultKeylet);
            if (!BEAST_EXPECT(sleVault))
                return;
            sleVault->at(sfLossUnrealized) = Number(0);
            ac.view().update(sleVault);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan impair must not decrease, and loan unimpair must not "
                "increase, loss unrealized"));
        }

        // ttLOAN_MANAGE (impair/unimpair): loss unrealized must move by
        // exactly the amount the loan owes to the vault. Both directions are
        // exercised: impair grows the paper loss, unimpair shrinks it. The
        // bookkeeping is off by 50 (owed is 100, delta is 50), so the sign
        // check at loan-manage-common passes but the magnitude check trips.
        {
            struct Case
            {
                std::uint32_t txFlag;
                std::uint32_t beforeLoanFlags;
                std::uint32_t afterLoanFlags;
                Number beforeLossUnrealized;
                Number afterLossUnrealized;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                {.txFlag = tfLoanImpair,
                 .beforeLoanFlags = 0,
                 .afterLoanFlags = lsfLoanImpaired,
                 .beforeLossUnrealized = Number(0),
                 .afterLossUnrealized = Number(50),
                 .expected =
                     "loan impair must increase loss unrealized by exactly the amount the loan "
                     "owes to the vault"},
                {.txFlag = tfLoanUnimpair,
                 .beforeLoanFlags = lsfLoanImpaired,
                 .afterLoanFlags = 0,
                 .beforeLossUnrealized = Number(100),
                 .afterLossUnrealized = Number(50),
                 .expected =
                     "loan unimpair must decrease loss unrealized by exactly the amount the loan "
                     "owes to the vault"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                // Seed the vault with the initial lossUnrealized so the
                // apply-view mutation registers as a bounded delta.
                {
                    auto const sleVaultRead = ov.read(vaultKeylet);
                    if (!BEAST_EXPECT(sleVaultRead))
                        continue;
                    auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                    sleVault->at(sfLossUnrealized) = c.beforeLossUnrealized;
                    ov.rawReplace(sleVault);
                }

                // Seed a loan in the base view with `owedToVault` == 100 so
                // the magnitude check has a definite target.
                auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(100);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.beforeLoanFlags);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_MANAGE, [&](STObject& t) { t.setFieldU32(sfFlags, c.txFlag); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                // Modify lossUnrealized by the wrong delta (50 instead of 100).
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    continue;
                sleVault->at(sfLossUnrealized) = c.afterLossUnrealized;
                ac.view().update(sleVault);

                // Flip the impair flag on the loan so the flag-transition
                // check passes and the magnitude check is reached.
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.afterLoanFlags);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // ttLOAN_MANAGE (impair): a successful impair must leave the loan
        // flagged as impaired and must not target an already-impaired loan.
        // Each violation is set up bespoke because the shared harness cannot
        // seed a pre-existing loan whose lsfLoanImpaired bit we can control
        // across the before/after boundary.
        {
            struct Case
            {
                std::uint32_t before;
                std::uint32_t after;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                // Impair a non-impaired loan but forget to set the flag.
                {.before = 0,
                 .after = 0,
                 .expected =
                     "LoanManage(tfLoanImpair) must set lsfLoanImpaired on a non-impaired loan"},
                // Impair an already-impaired loan (regardless of the resulting flag).
                {.before = lsfLoanImpaired,
                 .after = lsfLoanImpaired,
                 .expected =
                     "LoanManage(tfLoanImpair) must set lsfLoanImpaired on a non-impaired loan"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanImpair); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // Flag-transition scoping (featureLendingProtocolV1_1).
        // lsfLoanImpaired may only change under ttLOAN_MANAGE or ttLOAN_PAY
        // (LoanPay unimpairs before applying the payment when the loan was
        // impaired); lsfLoanDefault may only change under ttLOAN_MANAGE.
        // Any other transaction that moves either flag is manufacturing
        // state.  Setup mirrors the impair/unimpair blocks: seed a
        // pre-existing loan with a specific flag, then flip the flag under
        // an out-of-scope transaction type.  The out-of-scope tx is
        // ttACCOUNT_SET, chosen because it is a valid transaction type
        // that lives outside the loan-manage / loan-pay switch.
        {
            struct Case
            {
                std::uint32_t before;
                std::uint32_t after;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                {.before = 0,
                 .after = lsfLoanImpaired,
                 .expected = "lsfLoanImpaired changed outside LoanManage or LoanPay"},
                {.before = lsfLoanImpaired,
                 .after = 0,
                 .expected = "lsfLoanImpaired changed outside LoanManage or LoanPay"},
                // lsfLoanDefault: only the unset->set direction is exercised
                // here because the reverse (set->unset) is separately blocked
                // by NoModifiedUnmodifiableFields, whose fatal log fires first
                // and would mask the ValidLoan message under test.
                {.before = 0,
                 .after = lsfLoanDefault,
                 .expected = "lsfLoanDefault changed outside LoanManage"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{ttACCOUNT_SET, [](STObject&) {}};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // ttLOAN_MANAGE (unimpair): the mirror of the impair check. A
        // successful unimpair must leave the loan without lsfLoanImpaired and
        // must not target a non-impaired loan. Setup mirrors the impair block
        // above.
        {
            struct Case
            {
                std::uint32_t before;
                std::uint32_t after;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                // Unimpair an impaired loan but forget to clear the flag.
                {.before = lsfLoanImpaired,
                 .after = lsfLoanImpaired,
                 .expected =
                     "LoanManage(tfLoanUnimpair) must clear lsfLoanImpaired on an impaired loan"},
                // Unimpair a non-impaired loan (regardless of the resulting flag).
                {.before = 0,
                 .after = 0,
                 .expected =
                     "LoanManage(tfLoanUnimpair) must clear lsfLoanImpaired on an impaired loan"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    sleLoan->setFieldU32(sfFlags, c.before);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanUnimpair); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->setFieldU32(sfFlags, c.after);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(c.expected));
            }
        }

        // ttLOAN_MANAGE (default): a defaulted loan atomically enters a
        // terminal state, which drops sfNextPaymentDueDate from the ledger
        // entry. Seed a loan that already carries lsfLoanDefault so the
        // "must newly set" check passes, then leave sfNextPaymentDueDate
        // present and non-zero on the after-image; the residual due-date
        // check must then fire.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            // Pre-insert a loan that is not yet defaulted but has a
            // NextPaymentDueDate set; the apply-view mutation below flips
            // lsfLoanDefault (so the "must newly set" check passes) while
            // leaving the due date behind.
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->setFieldU32(sfNextPaymentDueDate, 123);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "Loan with zero payments must have zero next payment due date"));
        }

        // ttLOAN_MANAGE (default): loss unrealized must not increase. A default
        // realizes the paper loss (or leaves it at zero for a non-impaired
        // loan); it can never grow it.
        doInvariantCheck(
            {"loan default must not increase loss unrealized"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{.lossUnrealized = 5});
            },
            XRPAmount{},
            STTx{ttLOAN_MANAGE, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLoanDefault); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: the vault (pseudo-account) balance must change
        doInvariantCheck(
            {"loan pay must change vault balance"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY pre-featureLendingProtocolV1_1: finalizeLoanPay short-
        // circuits and returns success. The same "no vault balance change"
        // state that trips the check under V1_1 must be silently accepted
        // here.
        doInvariantCheck(
            makeEnv(all_ - featureLendingProtocolV1_1),
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(ac.view(), keylet, Adjustments{});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tesSUCCESS, tesSUCCESS},
            precloseXrp);

        // ttLOAN_PAY: cash is credited to the vault but no loan is touched.
        // The vault-balance check passes because a real inflow was recorded;
        // ValidLoan catches the missing loan.
        doInvariantCheck(
            {"lending transaction must touch exactly one loan"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 50,
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(50)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: cash is credited to the vault and a loan is created
        // rather than modified. This object-existence rule applies on both
        // invariant passes.
        doInvariantCheck(
            {"Loan created by a transaction other than LoanSet"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 50,
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50},
                        .createLoan = LoanParams{
                            .principalOutstanding = 100,
                            .totalValueOutstanding = 100,
                            .borrower = a1.id(),
                        }});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(50)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets available must track the real vault balance. The
        // vault balance (pseudo-account) grows by 50 but assets available is
        // bumped by 60, so the two no longer add up.
        doInvariantCheck(
            {"vault balance and assets available must add up"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = 60,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(100)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets available must increase
        doInvariantCheck(
            {"loan pay must increase assets available"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsAvailable = -100,
                        .vaultAssets = -100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = 100}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets available increases by more than the amount paid
        doInvariantCheck(
            {"loan pay must not increase assets available by more than the amount paid"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 300,
                        .assetsAvailable = 300,
                        .vaultAssets = 300,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -300}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(100)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: shares outstanding changes
        //
        // Escalates to tef for the same harness reason as the ttLOAN_MANAGE
        // shares-change case above: ValidMPTPayment fires on the second
        // pass because visitEntry-accumulated MPT deltas double.
        doInvariantCheck(
            {"shares outstanding must only change by deposit, withdraw, or clawback"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 100,
                        .sharesTotal = 10,
                        .vaultAssets = 100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -100},
                        .accountShares = AccountAmount{.account = a2.id(), .amount = 10}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: loss unrealized driven negative. The cash inflow is
        // valid, but loss unrealized is set below zero.
        doInvariantCheck(
            {"loss unrealized must not be negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                return kAdjust(
                    ac.view(),
                    keylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 100,
                        .lossUnrealized = -1,
                        .vaultAssets = 100,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -100}});
            },
            XRPAmount{},
            STTx{ttLOAN_PAY, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseXrp);

        // ttLOAN_PAY: assets outstanding must match the cash received and the
        // change in the paid loan's claim on the vault. A payment only moves
        // value between the vault's available cash and its claim on the loan,
        // so bumping assets outstanding by more than that must be caught. This
        // needs a loan that already exists in the base ledger (so modifying it
        // is seen as a before/after change), which the shared harness cannot
        // set up, hence the bespoke view construction below.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
            // Insert a pre-existing loan into the base view; modifying it in the
            // apply view is then seen as a loan modification (before/after).
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(100)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Cash received: assets available and the vault balance both grow by
            // 60, paid by a2. The paid loan's claim drops by 60 (total value
            // 150 -> 90). Conservation requires assets outstanding to be
            // unchanged, but we bump it by 10 to violate the identity.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsTotal = 10,
                        .assetsAvailable = 60,
                        .vaultAssets = 60,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -60}})))
                return;

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(40);
            sleLoan->at(sfTotalValueOutstanding) = Number(90);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay assets outstanding must match the cash received and "
                "the change in the amount the loan owes to the vault"));
            // The pre-inserted loan carries a default (zero) sfLoanBrokerID,
            // which does not resolve to a live broker; the broker-existence
            // check must therefore also fire in the same walk.
            BEAST_EXPECT(sink.messages().str().contains("loan pay loan broker must exist"));
        }

        // ttLOAN_PAY: the vault's claim on the loan may only shrink. A payment
        // pays the loan down, so total value outstanding (net of management
        // fee) can only fall. The bespoke setup mirrors the conservation test
        // above: a pre-existing loan is inserted so modifying it registers as a
        // before/after change.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Cash inflow of 50 (paid by a2), assets outstanding grows by 100
            // to keep the conservation identity honest (deltatotal - deltaavailable -
            // deltaclaim = 100 - 50 - 50 = 0). Push both principal (100 → 150)
            // and total value (150 → 200): under either accounting basis the
            // vault's claim grows by 50, which the sign check must reject.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsTotal = 100,
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                return;

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(150);
            sleLoan->at(sfTotalValueOutstanding) = Number(200);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay must not increase the amount the loan owes to the vault"));
        }

        // ttLOAN_PAY (non-full repayment): NextPaymentDueDate must advance by
        // a positive multiple of PaymentInterval. The earlier "strictly
        // decrease PrincipalOutstanding" and "decrease PaymentRemaining"
        // checks are satisfied so the due-date check is the one that fires.
        // Two failure modes: the due date does not advance at all, and the
        // due date advances by an amount that is not a multiple of
        // PaymentInterval.
        {
            struct Case
            {
                std::uint32_t beforeDue;
                std::uint32_t afterDue;
                std::string label;
            };
            auto const cases = std::to_array<Case>({
                {.beforeDue = 1000, .afterDue = 1000, .label = "no advance"},
                {.beforeDue = 1000, .afterDue = 1005, .label = "non-multiple advance"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                BEAST_EXPECT(precloseXrp(a1, a2, env));
                env.close();

                OpenView ov{*env.current()};

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(ov.seq()));
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a1.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->setFieldU32(sfPaymentRemaining, 2);
                    sleLoan->setFieldU32(sfPaymentInterval, 10);
                    sleLoan->setFieldU32(sfNextPaymentDueDate, c.beforeDue);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                // Cash inflow of 50 with matching bookkeeping so the earlier
                // conservation checks pass and the due-date check is reached.
                auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
                if (!BEAST_EXPECT(kAdjust(
                        ac.view(),
                        vaultKeylet,
                        Adjustments{
                            .assetsTotal = 0,
                            .assetsAvailable = 50,
                            .vaultAssets = 50,
                            .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                    continue;

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                // Strictly decrease principal and payments-remaining so the
                // earlier ttLOAN_PAY checks pass.
                sleLoan->at(sfPrincipalOutstanding) = Number(50);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                sleLoan->setFieldU32(sfNextPaymentDueDate, c.afterDue);
                ac.view().update(sleLoan);

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(
                    "loan pay must advance NextPaymentDueDate by a positive "
                    "multiple of PaymentInterval on a non-full-repayment"));
            }
        }

        // ttLOAN_PAY: the vault, the loan-broker pseudo-account and the
        // loan-broker owner are the only three destinations of a payment; their
        // combined inflow can never exceed tx[sfAmount].  Set up a real loan
        // broker so we can address its pseudo-account, then dispense 60 to the
        // vault and 50 to the broker pseudo — a total of 110 for a tx[sfAmount]
        // of 100, tripping the check.  The setup is bespoke because the shared
        // harness does not create loan brokers.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Insert a pre-existing loan pointing at the real broker so
            // finalizeLoanPay's broker lookup succeeds and the inflow-sum
            // check is actually reached.
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(100)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault balance and assetsAvailable both +60, borrower -110 (60 to
            // vault, 50 to broker pseudo).  The loan's claim drops by 60
            // (total value 150 -> 90) so assets outstanding stays at zero
            // delta and the assets-outstanding-vs-claim identity holds.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = 60,
                        .vaultAssets = 60,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -110}})))
                return;

            auto sleBrokerPseudo = ac.view().peek(keylet::account(brokerPseudoId));
            if (!BEAST_EXPECT(sleBrokerPseudo))
                return;
            (*sleBrokerPseudo)[sfBalance] = *(*sleBrokerPseudo)[sfBalance] + 50;
            ac.view().update(sleBrokerPseudo);

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(40);
            sleLoan->at(sfTotalValueOutstanding) = Number(90);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay vault and broker must not receive more than the amount paid"));
        }

        // ttLOAN_PAY: LossUnrealized must fall by exactly the pre-transaction
        // amount the loan owed to the vault when the loan was impaired (a
        // successful payment implicitly unimpairs), or stay unchanged
        // otherwise. Two bespoke cases mirror the impair/unimpair magnitude
        // tests; every other loan-pay identity is lined up so only the
        // LossUnrealized magnitude check trips.
        {
            struct Case
            {
                std::uint32_t beforeLoanFlags;
                Number beforeLossUnrealized;
                Number afterLossUnrealized;
            };
            auto const cases = std::to_array<Case>({
                // Non-impaired: expected delta is 0, but we drop LossUnrealized
                // by 5.
                {.beforeLoanFlags = 0,
                 .beforeLossUnrealized = Number(5),
                 .afterLossUnrealized = Number(0)},
                // Impaired: the vault is cash-basis, so owedToVault is the
                // principal only (100) and the expected delta is -100. We drop
                // LossUnrealized by 50 instead.
                {.beforeLoanFlags = lsfLoanImpaired,
                 .beforeLossUnrealized = Number(150),
                 .afterLossUnrealized = Number(100)},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                env.close();

                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
                if (!BEAST_EXPECT(env.le(brokerKeylet)))
                    return;
                env.close();

                auto const sleBrokerBase = env.le(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerBase))
                    return;
                auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

                Vault const vault{env};
                env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
                env.close();

                OpenView ov{*env.current()};

                // Base LossUnrealized and broker DebtTotal reflect an
                // outstanding loan whose owedToVault is 150 (150 total value
                // less zero management fee); the base broker DebtTotal must
                // match so the delta DebtTotal identity holds after the payment.
                // AssetsAvailable and pseudoAccount balance are reduced by
                // the same 150 so the vault's own accounting (assetsAvailable
                // + owedToVault == assetsTotal) is consistent in the base.
                AccountID pseudoId;
                {
                    auto const sleVaultRead = ov.read(vaultKeylet);
                    if (!BEAST_EXPECT(sleVaultRead))
                        continue;
                    pseudoId = sleVaultRead->at(sfAccount);
                    auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                    sleVault->at(sfLossUnrealized) = c.beforeLossUnrealized;
                    sleVault->at(sfAssetsAvailable) =
                        *sleVault->at(sfAssetsAvailable) - Number(150);
                    ov.rawReplace(sleVault);
                }
                {
                    auto const slePseudoRead = ov.read(keylet::account(pseudoId));
                    if (!BEAST_EXPECT(slePseudoRead))
                        continue;
                    auto slePseudo = std::make_shared<SLE>(*slePseudoRead);
                    slePseudo->at(sfBalance) = slePseudo->at(sfBalance) - XRPAmount(150);
                    ov.rawReplace(slePseudo);
                }
                {
                    auto const sleBrokerRead = ov.read(brokerKeylet);
                    if (!BEAST_EXPECT(sleBrokerRead))
                        continue;
                    auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                    sleBroker->at(sfDebtTotal) = Number(150);
                    ov.rawReplace(sleBroker);
                }
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->setFieldU32(sfPaymentRemaining, 2);
                    sleLoan->setFieldU32(sfPaymentInterval, 10);
                    sleLoan->setFieldU32(sfNextPaymentDueDate, 1000);
                    sleLoan->setFieldU32(sfFlags, c.beforeLoanFlags);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                // Cash inflow of 50 with matching bookkeeping: vault balance
                // +50, assets available +50, assets total unchanged so the
                // conservation identity (delta AssetsTotal − delta AssetsAvailable
                // − delta owedToVault == 0) holds.
                if (!BEAST_EXPECT(kAdjust(
                        ac.view(),
                        vaultKeylet,
                        Adjustments{
                            .assetsAvailable = 50,
                            .vaultAssets = 50,
                            .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                    continue;

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                // Principal and payments-remaining strictly decrease so the
                // non-full-repayment checks pass; total value drops by the
                // same 50 to keep interest-due nonnegative.
                sleLoan->at(sfPrincipalOutstanding) = Number(50);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                sleLoan->setFieldU32(sfNextPaymentDueDate, 1010);
                // Clear the impaired flag if it was set - a successful pay
                // implicitly unimpairs. Setting flags to 0 in either case
                // is consistent (unimpair preserves 0->0).
                sleLoan->setFieldU32(sfFlags, 0);
                ac.view().update(sleLoan);

                // Broker's DebtTotal falls by 50 to match delta owedToVault.
                {
                    auto sleBroker = ac.view().peek(brokerKeylet);
                    if (!BEAST_EXPECT(sleBroker))
                        continue;
                    sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(50);
                    ac.view().update(sleBroker);
                }

                // Deliberately mis-set LossUnrealized: the magnitude check
                // is the only one left to trip.
                {
                    auto sleVault = ac.view().peek(vaultKeylet);
                    if (!BEAST_EXPECT(sleVault))
                        continue;
                    sleVault->at(sfLossUnrealized) = c.afterLossUnrealized;
                    ac.view().update(sleVault);
                }

                auto transactor = makeTransactor(ac);
                if (!BEAST_EXPECT(transactor))
                    continue;
                TER const result = transactor->checkInvariants(
                    tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
                BEAST_EXPECT(result == tecINVARIANT_FAILED);
                BEAST_EXPECT(sink.messages().str().contains(
                    "loan pay must decrease loss unrealized by the "
                    "pre-transaction amount the loan owed to the vault when "
                    "impaired, or leave it unchanged otherwise"));
            }
        }

        // ttLOAN_PAY: the broker's sfDebtTotal must fall by exactly the
        // amount the loan's owedToVault fell. Every other loan-pay identity
        // is lined up so only the DebtTotal-tracking check trips. The broker
        // is touched so the "modify exactly the loan's broker" cardinality
        // guard passes.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Base DebtTotal mirrors the loan's owedToVault (150).
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(150);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->setFieldU32(sfPaymentRemaining, 2);
                sleLoan->setFieldU32(sfPaymentInterval, 10);
                sleLoan->setFieldU32(sfNextPaymentDueDate, 1000);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                return;

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(50);
            sleLoan->at(sfTotalValueOutstanding) = Number(100);
            sleLoan->setFieldU32(sfPaymentRemaining, 1);
            sleLoan->setFieldU32(sfNextPaymentDueDate, 1010);
            ac.view().update(sleLoan);

            // Broker DebtTotal drops by 30 (should drop by 50).
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(30);
                ac.view().update(sleBroker);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan pay broker debt total must track the change in the "
                "amount the loan owes to the vault"));
        }

        // Non-transferable vault shares (tfVaultShareNonTransferable at
        // creation, so the share issuance carries no lsfMPTCanTransfer bit)
        // may only be issued or burned by a Vault deposit / withdraw /
        // clawback / create / delete flow. Any other transaction touching
        // one of the holder MPToken positions of that issuance must trip
        // the check.
        {
            Keylet mptokenKeylet = keylet::amendments();
            Keylet vaultKeylet = keylet::amendments();
            Preclose const createNonTransferableVault =
                [&mptokenKeylet, &vaultKeylet, this](
                    Account const& a1, Account const& a2, Env& env) -> bool {
                Vault const vault{env};
                auto [tx, vk] = vault.create(
                    {.owner = a1, .asset = xrpIssue(), .flags = tfVaultShareNonTransferable});
                vaultKeylet = vk;
                env(tx);
                env.close();
                if (!BEAST_EXPECT(env.le(vaultKeylet)))
                    return false;
                // a2 deposits so a share MPToken position exists for
                // that holder in the base view.
                env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(100)}));
                env.close();

                auto const sleVault = env.le(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return false;
                mptokenKeylet = keylet::mptoken(sleVault->at(sfShareMPTID), a2.id());
                return BEAST_EXPECT(env.le(mptokenKeylet));
            };

            // Touch the share MPToken under a ttVAULT_SET (MustModifyVault
            // txn not excluded from the non-transferable check). The vault
            // itself is also touched so afterVault_ is populated; the
            // finalize walk sees touchedShareIssuances_ populated and the
            // share issuance carries no lsfMPTCanTransfer, so the check fires.
            doInvariantCheck(
                {"non-transferable vault shares must not move outside of "
                 "deposit, withdraw, or clawback"},
                [&mptokenKeylet, &vaultKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sleMpt = ac.view().peek(mptokenKeylet);
                    if (!sleMpt)
                        return false;
                    ac.view().update(sleMpt);
                    auto sleVault = ac.view().peek(vaultKeylet);
                    if (!sleVault)
                        return false;
                    ac.view().update(sleVault);
                    return true;
                },
                XRPAmount{},
                STTx{ttVAULT_SET, [](STObject&) {}},
                // ValidVault::finalize skips all checks when the incoming
                // result is not tesSUCCESS, so the second pass passes and the
                // result stays tec.
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createNonTransferableVault);
        }

        // ttLOAN_MANAGE (default): the first-loss capital the vault receives
        // comes out of the loan-broker pseudo-account, so the two balances
        // must move by exactly opposite amounts. Credit the vault by 50 while
        // leaving the broker pseudo-account untouched: the residual is 50,
        // not zero, and the cover-conservation check must fire. The setup is
        // bespoke to give the invariant a real broker for the loan lookup.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(150);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault balance and assetsAvailable both +50 (as if first-loss
            // capital were returned), sourced from a2 rather than the broker
            // pseudo-account. The broker balance stays put, so the two
            // deltas do not cancel.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{
                        .assetsAvailable = 50,
                        .vaultAssets = 50,
                        .accountAssets = AccountAmount{.account = a2.id(), .amount = -50}})))
                return;

            // Modify the loan (before/after) so exactlyOneLoan passes.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must move the first-loss capital from the loan "
                "broker to the vault"));
        }

        // ttLOAN_MANAGE (default): loss unrealized must move by exactly
        // -beforeLoan.owedToVault when the loan was impaired, or stay
        // unchanged when it was not. Base loan is not impaired, so the
        // expected delta is zero, yet the apply view drops loss unrealized
        // by 5, so the magnitude check trips. Other default-side identities
        // (cover, DebtTotal, vault-side conservation, cardinality) are all
        // lined up.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            // Prime the broker's cover so it can release first-loss capital
            // in the apply view.
            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Seed the base view with a non-impaired loan carrying a fully
            // funded principal, and a nonzero LossUnrealized on the vault so
            // a decrease in the apply view is a bounded delta.
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfLossUnrealized) = Number(5);
                ov.rawReplace(sleVault);
            }
            // Seed the broker with a DebtTotal that mirrors the loan's
            // owedToVault so the apply-view -100 lands at zero rather than
            // going negative (which would trip a separate ValidLoanBroker
            // check).
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault gets 100 first-loss capital: balance +100 and available
            // +100. Assets total is unchanged because the +100 inflow is
            // netted against the -100 write-off, so
            // `delta AssetsTotal − delta AssetsAvailable − delta owedToVault
            //   == 0 − 100 − (−100) == 0`.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 100, .vaultAssets = 100})))
                return;

            // Broker pseudo-account -100 mirrors the vault gain (cover
            // conservation).
            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }

            // Broker: sfCoverAvailable drops by 100 (matches vault +100),
            // sfDebtTotal drops by 100 (matches delta owedToVault = -100).
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
                ac.view().update(sleBroker);
            }

            // Zero the loan and default it.
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            // Drop LossUnrealized from 5 to 0. Loan was not impaired, so
            // expected delta is 0, actual delta is -5 -> magnitude trips.
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                sleVault->at(sfLossUnrealized) = Number(0);
                ac.view().update(sleVault);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must decrease loss unrealized by the "
                "pre-transaction amount the loan owed to the vault when "
                "impaired, or leave it unchanged otherwise"));
        }

        // ttLOAN_MANAGE (default): mirror of the previous test for the
        // impaired branch. The base loan carries lsfLoanImpaired with
        // owedToVault = 100, so the paper loss (LossUnrealized = 100) must
        // fall by exactly 100 on default. The apply view only drops it by
        // 50, so the residual is 50 and the magnitude check trips.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Base LossUnrealized == 100 (the impaired loan's owedToVault),
            // matching the state a real impair would produce.
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfLossUnrealized) = Number(100);
                ov.rawReplace(sleVault);
            }
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                sleLoan->setFieldU32(sfFlags, lsfLoanImpaired);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 100, .vaultAssets = 100})))
                return;

            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
                ac.view().update(sleBroker);
            }
            // Zero the loan and default it (clear the impaired flag as the
            // realized loss is now on the ledger).
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            // Drop LossUnrealized from 100 to 50 (should drop to 0).
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                sleVault->at(sfLossUnrealized) = Number(50);
                ac.view().update(sleVault);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must decrease loss unrealized by the "
                "pre-transaction amount the loan owed to the vault when "
                "impaired, or leave it unchanged otherwise"));
        }

        // ttLOAN_MANAGE (default): under featureLendingProtocolV1_1 the
        // invariant expects the loan's broker to have been touched so both
        // before/after snapshots are captured. A defaulted loan that leaves
        // the broker SLE untouched (a broken transactor) must be flagged.
        // Other default-side identities fire alongside because the setup is
        // deliberately minimal, but the target message is what we assert on.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Zero the loan and mark it defaulted so the flag-transition and
            // paid-off checks pass and the broker-cardinality check is
            // reached.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
            ac.view().update(sleLoan);

            // The vault must be touched so before/afterVault_ are captured and
            // finalize dispatches to finalizeLoanManage at all.
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                ac.view().update(sleVault);
            }

            // Deliberately do not touch the broker SLE. before/afterBroker_
            // both stay empty, tripping the cardinality guard.

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must modify exactly the loan's broker"));
        }

        // ttLOAN_MANAGE (default): if the broker released first-loss capital
        // (its sfCoverAvailable moved) the vault balance ledger entry must
        // reflect it. Broker's cover changes in the apply view but neither
        // the vault's pseudo-account balance nor its accounting fields do,
        // so deltaAssets(pseudoId) is nullopt and the "must change vault
        // balance when first-loss capital is returned" guard trips.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            // Seed a non-impaired defaulting loan and a matching DebtTotal
            // on the broker.
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Broker cover drops by 100 (as if capital were released) but
            // the vault pseudo-account balance is not touched -
            // maybeVaultDeltaAssets is nullopt and the guard fires.
            auto sleBroker = ac.view().peek(brokerKeylet);
            if (!BEAST_EXPECT(sleBroker))
                return;
            sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
            sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
            ac.view().update(sleBroker);

            // Zero the loan and default it so paid-off and flag-transition
            // checks pass.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
            ac.view().update(sleLoan);

            // Touch the vault SLE (without moving its pseudo-account balance)
            // so before/afterVault_ are captured and finalize dispatches to
            // finalizeLoanManage.
            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                ac.view().update(sleVault);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must change vault balance when first-loss "
                "capital is returned"));
        }

        // ttLOAN_MANAGE (default): sfAssetsAvailable must grow by exactly
        // the amount the broker released (DefaultCovered = delta broker
        // sfCoverAvailable). Broker releases 100 and the vault balance
        // grows by 100, but assets available is only credited with 50, so
        // the identity fires. The "adds-up" check fires alongside because
        // vault balance and assets available diverge; the assertion is
        // scoped to our target message.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault: pseudo-account balance +100 (real cash inflow), but
            // AssetsAvailable only +50. AssetsTotal is unchanged so the
            // vault-side conservation identity would want
            // delta owedToVault == delta AssetsTotal − delta AssetsAvailable == -50.
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 50, .vaultAssets = 100})))
                return;

            // Broker pseudo-account -100 mirrors the vault gain (cover
            // conservation intact).
            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }
            // Broker: cover -100 mirrors the pseudo-account balance (the
            // sfCoverAvailable == pseudoBalance invariant stays intact).
            // sfDebtTotal drops by 100 to match delta owedToVault.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(100);
                ac.view().update(sleBroker);
            }
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default must increase assets available by the default "
                "covered amount"));
        }

        // ttLOAN_MANAGE (default): the broker's sfDebtTotal must fall by
        // exactly the amount the loan owed to the vault. Every other
        // default-side identity (cover conservation, adds-up, DefaultCovered
        // == delta AssetsAvailable, vault-side conservation) balances; only
        // sfDebtTotal is deliberately mis-adjusted (drops by 50 instead of
        // 100), so the DebtTotal-tracking check trips in isolation.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
            if (!BEAST_EXPECT(env.le(brokerKeylet)))
                return;
            env.close();

            using namespace loan_broker;
            env(coverDeposit(a1, brokerKeylet.key, XRP(100)));
            env.close();

            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            auto const brokerPseudoId = sleBrokerBase->at(sfAccount);

            Vault const vault{env};
            env(vault.deposit({.depositor = a2, .id = vaultKeylet.key, .amount = XRP(500)}));
            env.close();

            OpenView ov{*env.current()};

            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = Number(100);
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Vault gets 100 first-loss capital: balance +100, available
            // +100, total unchanged (write-off nets the inflow).
            if (!BEAST_EXPECT(kAdjust(
                    ac.view(),
                    vaultKeylet,
                    Adjustments{.assetsAvailable = 100, .vaultAssets = 100})))
                return;

            // Broker pseudo-account -100 (cover conservation).
            {
                auto slePseudo = ac.view().peek(keylet::account(brokerPseudoId));
                if (!BEAST_EXPECT(slePseudo))
                    return;
                slePseudo->at(sfBalance) = *slePseudo->at(sfBalance) - 100;
                ac.view().update(slePseudo);
            }
            // Broker: cover -100 (matches delta AssetsAvailable), but
            // sfDebtTotal only drops by 50, not the expected 100.
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfCoverAvailable) = *sleBroker->at(sfCoverAvailable) - Number(100);
                sleBroker->at(sfDebtTotal) = *sleBroker->at(sfDebtTotal) - Number(50);
                ac.view().update(sleBroker);
            }
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains(
                "loan default broker debt total must track the change in the "
                "amount the loan owes to the vault"));
        }

        // ttLOAN_MANAGE (default): the write-off is rounded downward at the
        // pre-default AssetsTotal scale. A near-total IOU default can leave
        // valid positive dust while moving the posterior AssetsTotal to a much
        // finer scale. The dust must be bounded by the former scale rather than
        // compared with one unit at the posterior scale.
        {
            Env env{*this, all_ | featureLendingProtocolV1_1};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const borrower{"borrower"};
            env.fund(XRP(1000), issuer, owner, borrower);
            env.close();

            PrettyAsset const iouAsset{issuer["IOU"]};
            auto const brokerKeylet = createLoanBroker(owner, env, iouAsset);
            auto const sleBrokerBase = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBrokerBase))
                return;
            auto const vaultKeylet = keylet::vault(sleBrokerBase->at(sfVaultID));
            env.close();

            Number const assetsTotalBefore{1, 1};
            Number const loanOwed{9'999'999'999'999'999LL, -15};
            Number const assetsTotalAfter{1, -14};
            auto const beforeScale = scale(assetsTotalBefore, iouAsset);
            auto const afterScale = scale(assetsTotalAfter, iouAsset);
            Number const residual = (assetsTotalAfter - assetsTotalBefore) - (-loanOwed);
            Number const beforeTolerance{1, beforeScale};
            Number const afterTolerance{1, afterScale};

            BEAST_EXPECT(afterScale < beforeScale);
            BEAST_EXPECT(residual > beast::kZero && residual < beforeTolerance);
            BEAST_EXPECT(residual > afterTolerance);

            OpenView ov{*env.current()};
            {
                auto const sleVaultRead = ov.read(vaultKeylet);
                if (!BEAST_EXPECT(sleVaultRead))
                    return;
                auto sleVault = std::make_shared<SLE>(*sleVaultRead);
                sleVault->at(sfAssetsTotal) = assetsTotalBefore;
                sleVault->at(sfAssetsAvailable) = Number(0);
                ov.rawReplace(sleVault);
            }
            {
                auto const sleBrokerRead = ov.read(brokerKeylet);
                if (!BEAST_EXPECT(sleBrokerRead))
                    return;
                auto sleBroker = std::make_shared<SLE>(*sleBrokerRead);
                sleBroker->at(sfDebtTotal) = loanOwed;
                ov.rawReplace(sleBroker);
            }
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, borrower.id());
                sleLoan->at(sfPrincipalOutstanding) = loanOwed;
                sleLoan->at(sfTotalValueOutstanding) = loanOwed;
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            {
                auto sleVault = ac.view().peek(vaultKeylet);
                if (!BEAST_EXPECT(sleVault))
                    return;
                sleVault->at(sfAssetsTotal) = assetsTotalAfter;
                ac.view().update(sleVault);
            }
            {
                auto sleBroker = ac.view().peek(brokerKeylet);
                if (!BEAST_EXPECT(sleBroker))
                    return;
                sleBroker->at(sfDebtTotal) = Number(0);
                ac.view().update(sleBroker);
            }
            {
                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    return;
                sleLoan->at(sfPrincipalOutstanding) = Number(0);
                sleLoan->at(sfTotalValueOutstanding) = Number(0);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                sleLoan->setFieldU32(sfFlags, lsfLoanDefault);
                ac.view().update(sleLoan);
            }

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tesSUCCESS);
        }

        // ttLOAN_MANAGE (default): the invariant reads the broker through the
        // defaulted loan. If the loan carries a stale or zero LoanBrokerID
        // the lookup fails, so the check that guards the cover-conservation
        // step must report it explicitly rather than silently skip.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const vaultKeylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ov.seq()));
            // Pre-insert a loan carrying a default (zero) sfLoanBrokerID so
            // the invariant's broker lookup returns nullopt.
            auto const loanKeylet = keylet::loan(uint256{}, SeqProxy::rawSequence(1));
            {
                auto sleLoan = makeLoanSle(uint256{}, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{
                ttLOAN_MANAGE, [](STObject& t) { t.setFieldU32(sfFlags, tfLoanDefault); }};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            // Touch the vault so ValidVault::finalize enters
            // finalizeLoanManage; the tfLoanDefault path is what carries the
            // "loan default loan broker must exist" check we are asserting.
            auto sleVault = ac.view().peek(vaultKeylet);
            if (!BEAST_EXPECT(sleVault))
                return;
            ac.view().update(sleVault);

            // Modify the loan (before/after) so exactlyOneLoan passes and the
            // broker lookup is actually reached.
            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                return;
            sleLoan->at(sfPrincipalOutstanding) = Number(0);
            sleLoan->at(sfTotalValueOutstanding) = Number(0);
            sleLoan->setFieldU32(sfPaymentRemaining, 0);
            ac.view().update(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                return;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains("loan default loan broker must exist"));
        }

        // A loan may only be deleted by a LoanDelete transaction, and only once
        // it is fully paid off. Both branches are exercised by creating a real
        // loan in the Preclose (so it exists in the base ledger with outstanding
        // principal) and then erasing it in the Precheck.
        {
            Keylet loanKeylet = keylet::amendments();
            auto const precloseLoan = [&loanKeylet, this](
                                          Account const& a1, Account const& a2, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

                Vault const vault{env};
                auto [tx, vaultKeylet] = vault.create({.owner = a1, .asset = xrpAsset});
                env(tx);
                env.close();
                if (!BEAST_EXPECT(env.le(vaultKeylet)))
                    return false;

                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = xrpAsset(100)}));
                env.close();

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
                env(loan_broker::set(a1, vaultKeylet.key), Fee(kIncrement));
                env.close();
                auto const brokerSle = env.le(brokerKeylet);
                if (!BEAST_EXPECT(brokerSle))
                    return false;

                loanKeylet = keylet::loan(
                    brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
                env(loan::set(a2, brokerKeylet.key, xrpAsset(50).value()),
                    Sig(sfCounterpartySignature, a1),
                    Fee(env.current()->fees().base * 2));
                env.close();
                return BEAST_EXPECT(env.le(loanKeylet));
            };

            auto const eraseLoan = [&loanKeylet](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(loanKeylet);
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            };

            // Deleting the loan under any transaction type other than LoanDelete
            // (here the neutral ttACCOUNT_SET) is a violation, even while the
            // loan still has outstanding obligations: the transaction-type check
            // fires before the not-fully-paid-off check.
            doInvariantCheck(
                {"Loan deleted by a transaction other than LoanDelete"},
                eraseLoan,
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLoan);

            // Deleting the loan via LoanDelete while it still has outstanding
            // obligations is a violation: the transaction-type check passes and
            // the not-fully-paid-off check fires.
            doInvariantCheck(
                {"Loan deleted while not fully paid off"},
                eraseLoan,
                XRPAmount{},
                STTx{ttLOAN_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLoan);
        }

        // The not-fully-paid-off check inspects four fields disjunctively:
        // sfPaymentRemaining, sfTotalValueOutstanding, sfPrincipalOutstanding
        // and sfManagementFeeOutstanding. The block above covers the composite
        // case (all non-zero). Below, exercise each balance field on its own
        // with sfPaymentRemaining pinned to zero, so the check fires solely
        // on that disjunct. The base-view loan is seeded bespoke so its
        // fields can be shaped precisely.
        for (auto const field : {
                 &sfTotalValueOutstanding,
                 &sfPrincipalOutstanding,
                 &sfManagementFeeOutstanding,
             })
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            Account const a2{"A2"};
            env.fund(XRP(1000), a1, a2);
            BEAST_EXPECT(precloseXrp(a1, a2, env));
            env.close();

            OpenView ov{*env.current()};

            auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
            auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
            // Seed a loan whose only outstanding obligation is on `field`,
            // with sfPaymentRemaining already at zero so the paid-off
            // disjunct being tested is the balance field alone.
            {
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(*field) = Number(10);
                ov.rawInsert(sleLoan);
            }

            STTx const tx{ttLOAN_DELETE, [](STObject&) {}};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            CurrentTransactionRulesGuard const rulesGuard(ov.rules());

            auto sleLoan = ac.view().peek(loanKeylet);
            if (!BEAST_EXPECT(sleLoan))
                continue;
            ac.view().erase(sleLoan);

            auto transactor = makeTransactor(ac);
            if (!BEAST_EXPECT(transactor))
                continue;
            TER const result = transactor->checkInvariants(
                tesSUCCESS, XRPAmount{}, Transactor::InvariantScope::Full);
            BEAST_EXPECT(result == tecINVARIANT_FAILED);
            BEAST_EXPECT(sink.messages().str().contains("Loan deleted while not fully paid off"));
        }

        {
            Keylet loanKeylet = keylet::amendments();
            auto const precloseLoan = [&loanKeylet, this](
                                          Account const& a1, Account const& a2, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

                Vault const vault{env};
                auto [tx, vaultKeylet] = vault.create({.owner = a1, .asset = xrpAsset});
                env(tx);
                env.close();
                if (!BEAST_EXPECT(env.le(vaultKeylet)))
                    return false;

                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = xrpAsset(100)}));
                env.close();

                auto const brokerKeylet =
                    keylet::loanBroker(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
                env(loan_broker::set(a1, vaultKeylet.key), Fee(kIncrement));
                env.close();
                auto const brokerSle = env.le(brokerKeylet);
                if (!BEAST_EXPECT(brokerSle))
                    return false;

                loanKeylet = keylet::loan(
                    brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
                env(loan::set(a2, brokerKeylet.key, xrpAsset(50).value()),
                    Sig(sfCounterpartySignature, a1),
                    Fee(env.current()->fees().base * 2));
                env.close();
                return BEAST_EXPECT(env.le(loanKeylet));
            };

            // (XLS-66 3.1.5 precondition 1): a LoanBrokerDelete
            // transaction must not touch any loan. Broker delete requires
            // OwnerCount == 0 (no loans reference the broker); touching a
            // loan alongside the delete points at either an
            // OwnerCount-tracking bug or a spurious cascading write.
            doInvariantCheck(
                {"LoanBrokerDelete must not touch any loan"},
                [&loanKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(loanKeylet);
                    if (!sle)
                        return false;
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_DELETE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                precloseLoan);
        }

        STTx const loanSetTx{
            ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(0); }};

        // Loan interest due (total value less principal and management fee)
        // must never be negative. The loan object is created directly with
        // principal 100, total value 90 and management fee 0, so interest due
        // = 90 - 100 - 0 = -10 (< 0)
        // while every individual field stays non-negative.
        doInvariantCheck(
            {"Loan interest due is negative"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(90);
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ac.view().insert(sleLoan);
                return true;
            },
            XRPAmount{},
            loanSetTx);

        // Each of these loan STNumber fields must never be negative. The loan
        // is created directly with a single field set negative while the
        // paid-off bookkeeping is kept consistent, so that only the "<field>
        // is negative" check trips.
        for (auto const field : {
                 &sfLoanServiceFee,
                 &sfLatePaymentFee,
                 &sfClosePaymentFee,
                 &sfPrincipalOutstanding,
                 &sfTotalValueOutstanding,
                 &sfManagementFeeOutstanding,
             })
        {
            // The outstanding-balance fields also feed the paid-off checks, so
            // a loan carrying one must still have payments remaining; a loan
            // with only a negative fee stays fully paid off (zero remaining).
            bool const isOutstanding = *field == sfPrincipalOutstanding ||
                *field == sfTotalValueOutstanding || *field == sfManagementFeeOutstanding;
            doInvariantCheck(
                {field->getName() + " is negative"},
                [&, field](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                    sleLoan->at(*field) = Number(-10);
                    sleLoan->setFieldU32(sfPaymentRemaining, isOutstanding ? 1 : 0);
                    ac.view().insert(sleLoan);
                    return true;
                },
                XRPAmount{},
                loanSetTx);
        }

        // Mirror of the loop above for the strictly-positive constraint: a
        // loan's sfPeriodicPayment must always be > 0. Cover both boundary
        // failure modes (zero and negative).
        for (Number const& badValue : {Number(0), Number(-1)})
        {
            doInvariantCheck(
                {std::string{sfPeriodicPayment.getName()} + " is zero or negative"},
                [&, badValue](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                    sleLoan->at(sfPeriodicPayment) = badValue;
                    ac.view().insert(sleLoan);
                    return true;
                },
                XRPAmount{},
                loanSetTx);
        }

        // A loan with sfPaymentRemaining == 0 must be fully paid off in every
        // outstanding-balance dimension. Insert a bare loan that reports zero
        // payments remaining but still carries a non-zero principal owed; the
        // paid-off invariant must reject it before the later broker-existence
        // check has a chance to run.
        doInvariantCheck(
            {"Loan with zero payments remaining has not been paid off"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->at(sfPrincipalOutstanding) = Number(100);
                sleLoan->at(sfTotalValueOutstanding) = Number(100);
                sleLoan->at(sfPeriodicPayment) = Number(1);
                sleLoan->setFieldU32(sfPaymentRemaining, 0);
                ac.view().insert(sleLoan);
                return true;
            },
            XRPAmount{},
            loanSetTx);

        // Converse: a loan whose outstanding balances are all zero has been
        // fully paid off and must carry zero payments remaining. Insert a
        // fully-zeroed loan with sfPaymentRemaining = 1 to trip the check.
        doInvariantCheck(
            {"Fully paid off Loan still has payments remaining"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const brokerKeylet = keylet::loanBroker(a1.id(), SeqProxy::rawSequence(1));
                auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                sleLoan->setFieldU32(sfPaymentRemaining, 1);
                ac.view().insert(sleLoan);
                return true;
            },
            XRPAmount{},
            loanSetTx);

        // A loan must reference a live loan broker. A bare loan SLE is
        // inserted with every other loan-level field kept consistent so the
        // earlier ValidLoan checks pass; sfLoanBrokerID defaults to zero,
        // which resolves to no broker, and the broker-existence check trips.
        doInvariantCheck(
            {"Loan broker does not exist"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleLoan = makeLoanSle(uint256{}, 1, a2.id());
                ac.view().insert(sleLoan);
                return true;
            },
            XRPAmount{},
            loanSetTx);

        // A loan's broker must in turn reference a live vault. A real broker
        // is created in the preclose so its sfVaultID points at an existing
        // vault; the precheck then erases that vault and inserts a loan
        // referencing the broker, so the broker-existence check passes and
        // the broker-vault-existence check trips.
        {
            Keylet brokerKeylet = keylet::amendments();
            auto const precloseBroker = [&brokerKeylet, this](
                                            Account const& a1, Account const&, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                brokerKeylet = this->createLoanBroker(a1, env, xrpAsset);
                env.close();
                return BEAST_EXPECT(env.le(brokerKeylet));
            };

            doInvariantCheck(
                {"Loan broker vault does not exist"},
                [&brokerKeylet](Account const&, Account const&, ApplyContext& ac) {
                    auto sleBroker = ac.view().peek(brokerKeylet);
                    if (!sleBroker)
                        return false;
                    auto sleVault = ac.view().peek(keylet::vault(sleBroker->at(sfVaultID)));
                    if (!sleVault)
                        return false;
                    ac.view().erase(sleVault);

                    auto const loanKeylet =
                        keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                    auto sleLoan = std::make_shared<SLE>(loanKeylet);
                    sleLoan->at(sfLoanBrokerID) = brokerKeylet.key;
                    sleLoan->at(sfPrincipalOutstanding) = Number(0);
                    sleLoan->at(sfTotalValueOutstanding) = Number(0);
                    sleLoan->at(sfManagementFeeOutstanding) = Number(0);
                    sleLoan->at(sfPeriodicPayment) = Number(1);
                    sleLoan->setFieldU32(sfPaymentRemaining, 0);
                    ac.view().insert(sleLoan);
                    return true;
                },
                XRPAmount{},
                loanSetTx,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseBroker);
        }

        // ttVAULT_SET: owner is immutable (enforced by
        // NoModifiedUnmodifiableFields under fixCleanup3_4_0; sfOwner,
        // sfWithdrawalPolicy and sfScale pre-date featureLendingProtocolV1_1).
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setAccountID(sfOwner, a2.id());
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttVAULT_SET: withdrawal policy is immutable
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldU8(
                    sfWithdrawalPolicy,
                    static_cast<std::uint8_t>(sleVault->getFieldU8(sfWithdrawalPolicy) + 1));
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // ttVAULT_SET: scale is immutable
        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldU8(
                    sfScale, static_cast<std::uint8_t>(sleVault->getFieldU8(sfScale) + 1));
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

        // The pre-V1_1 vault fields must be enforced without waiting for
        // featureLendingProtocolV1_1. Run the withdrawal-policy mutation with
        // V1_1 removed but fixCleanup3_4_0 (part of testableAmendments) still enabled: the
        // invariant must still fire.
        doInvariantCheck(
            makeEnv(all_ - featureLendingProtocolV1_1),
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                sleVault->setFieldU8(
                    sfWithdrawalPolicy,
                    static_cast<std::uint8_t>(sleVault->getFieldU8(sfWithdrawalPolicy) + 1));
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseXrp);

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
                auto sleLoan = makeLoanSle(closedEndedBrokerKeylet.key, loanSeq, a1.id());
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

    // Minimal impaired-loan setup for testVaultLossExceedsGap.  Kept
    // inline here so this file has no dependency on LoanTestBase.
    Keylet
    makeImpairedVault(
        test::jtx::Account const& owner,
        test::jtx::Account const& borrower,
        test::jtx::Account const& issuer,
        test::jtx::Env& env)
    {
        using namespace test::jtx;

        env.fund(XRP(1'000'000), issuer, borrower);
        env.close();

        PrettyAsset const usd = issuer["USD"];
        STAmount const trustLimit{usd.raw(), Number{9'999'999'999'999'999LL}};
        env(trust(owner, trustLimit));
        env(trust(borrower, trustLimit));
        env.close();

        env(pay(issuer, owner, usd(100'000)));
        env(pay(issuer, borrower, usd(1'000)));
        env.close();

        // Under featureLendingProtocolV1_1 LoanBrokerSet::preclaim only
        // accepts closed-ended vaults. The 10-year investment window
        // covers this helper's 120 monthly payments so LoanSet's
        // RedemptionDate bound is satisfied.
        Vault const vault{env};
        auto [vaultTx, vaultKeylet, subscriptionDate] = vault.createClosedEnded(
            {.owner = owner,
             .asset = usd,
             .subscriptionOffset = std::chrono::seconds{60},
             .investmentWindow = std::chrono::seconds{10ull * 365ull * 24ull * 60ull * 60ull}});
        env(vaultTx);
        env.close();

        env(vault.deposit(
            {.depositor = owner, .id = vaultKeylet.key, .amount = usd(1'000).value()}));
        env.close();

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));

        {
            using namespace loan_broker;
            env(set(owner, vaultKeylet.key),
                kCoverRateMinimum(percentageToTenthBips(1)),
                kCoverRateLiquidation(xrpl::lending::kMaxCoverRate),
                Fee(env.current()->fees().base * 2));
            env.close();

            env(coverDeposit(owner, brokerKeylet.key, usd(10'000).value()),
                Fee(env.current()->fees().base * 2));
            env.close();
        }

        // LoanSet is gated on Investment; advance out of Subscription.
        vault.closePastSubscription(subscriptionDate);

        auto const brokerSle = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return vaultKeylet;

        auto const loanKeylet =
            keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));

        {
            using namespace loan;
            env(set(borrower, brokerKeylet.key, usd(100).value()),
                kCounterparty(owner),
                kInterestRate(TenthBips32{1000}),
                kPaymentTotal(120),
                kPaymentInterval(86400u * 30u),
                kGracePeriod(86400u * 30u),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 200));
            env.close();

            env(manage(owner, loanKeylet.key, tfLoanImpair));
            env.close();
        }

        return vaultKeylet;
    }

    // Regression test for the loss-vs-gap invariant relaxation introduced
    // by fixCleanup3_4_0.  Even with the one-unit tolerance, a loss value
    // exceeding (T - A) by more than one ULP must still fire.  Two
    // mutations exercise this:
    //   1. L = (T - A) * 2  — fires under both amendment settings.
    //   2. L = (T - A) + 2 * oneUnit  — fires post-amendment, catching
    //      any accidental widening of the tolerance beyond one unit.
    void
    testVaultLossExceedsGap()
    {
        testcase("vault loss exceeds gap (fixCleanup3_4_0 tolerance)");
        using namespace test::jtx;

        auto const kExpectedLog = std::vector<std::string>{
            "loss unrealized must not exceed the difference between assets "
            "outstanding and available"};

        for (auto const withFix : {false, true})
        {
            FeatureBitset amendments = all_;
            if (!withFix)
                amendments = amendments - fixCleanup3_4_0;

            // Variant 1: L = (T - A) * 2. Fires under both settings.
            {
                Keylet vaultKeylet = keylet::vault(uint256{});
                Account const issuer{"issuer_loss_gap"};
                Account const borrower{"borrower_loss_gap"};

                auto preclose = [&, this](Account const& owner, Account const&, Env& env) -> bool {
                    vaultKeylet = this->makeImpairedVault(owner, borrower, issuer, env);
                    return BEAST_EXPECT(env.le(vaultKeylet));
                };

                doInvariantCheck(
                    makeEnv(amendments),
                    kExpectedLog,
                    [&vaultKeylet](Account const&, Account const&, ApplyContext& ac) -> bool {
                        auto sle = ac.view().peek(vaultKeylet);
                        if (!sle)
                            return false;
                        Number const total = sle->at(sfAssetsTotal);
                        Number const available = sle->at(sfAssetsAvailable);
                        (*sle)[sfLossUnrealized] = (total - available) * 2;
                        ac.view().update(sle);
                        return true;
                    },
                    XRPAmount{},
                    STTx{
                        ttVAULT_DEPOSIT,
                        [&vaultKeylet](STObject& tx) {
                            tx.setFieldH256(sfVaultID, vaultKeylet.key);
                        }},
                    {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                    preclose,
                    TxAccount::A1);
            }

            // Variant 2: L = (T - A) + 2 * oneUnit at scale(T).  Must fire
            // post-fix because the tolerance is exactly one unit.  A
            // regression that widened it to two units would silently accept
            // this state.
            {
                Keylet vaultKeylet = keylet::vault(uint256{});
                Account const issuer{"issuer_loss_gap2"};
                Account const borrower{"borrower_loss_gap2"};

                auto preclose = [&, this](Account const& owner, Account const&, Env& env) -> bool {
                    vaultKeylet = this->makeImpairedVault(owner, borrower, issuer, env);
                    return BEAST_EXPECT(env.le(vaultKeylet));
                };

                doInvariantCheck(
                    makeEnv(amendments),
                    kExpectedLog,
                    [&vaultKeylet](Account const&, Account const&, ApplyContext& ac) -> bool {
                        auto sle = ac.view().peek(vaultKeylet);
                        if (!sle)
                            return false;
                        Number const total = sle->at(sfAssetsTotal);
                        Number const available = sle->at(sfAssetsAvailable);
                        Asset const asset = sle->at(sfAsset);
                        Number const oneUnit{1, scale(total, asset)};
                        (*sle)[sfLossUnrealized] = (total - available) + oneUnit * 2;
                        ac.view().update(sle);
                        return true;
                    },
                    XRPAmount{},
                    STTx{
                        ttVAULT_DEPOSIT,
                        [&vaultKeylet](STObject& tx) {
                            tx.setFieldH256(sfVaultID, vaultKeylet.key);
                        }},
                    {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                    preclose,
                    TxAccount::A1);
            }
        }
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
        testVaultLossExceedsGap();
        testVaultComputeCoarsestScale();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsVault, app, xrpl);

}  // namespace xrpl::test
