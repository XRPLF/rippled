#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
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
#include <chrono>
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
            // Broker the created loan references. Left unset when the test does
            // not depend on the broker resolving to a real ledger entry.
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
                // The vault key stands in for an unset broker: it keeps the loan
                // keylet distinct per vault while resolving to no broker.
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
        auto const precloseXrp = [&](Account const& a1,
                                     Account const& a2,
                                     Env& env,
                                     VaultVersion version = VaultVersion::CashBasis) -> bool {
            env.fund(XRP(1000), a3, a4);
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = a1, .asset = xrpIssue()});
            env(tx);
            env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a2, .id = keylet.key, .amount = XRP(10)}));
            env(vault.deposit({.depositor = a3, .id = keylet.key, .amount = XRP(10)}));
            return true;
        };

        auto const createClosedXrpBroker =
            [&](Account const& owner, Env& env) -> std::optional<std::pair<Keylet, Keylet>> {
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            auto const brokerKeylet = createLoanBroker(owner, env, xrpAsset);
            auto const sleBroker = env.le(brokerKeylet);
            if (!BEAST_EXPECT(sleBroker))
                return std::nullopt;
            auto const vaultKeylet = keylet::vault(sleBroker->at(sfVaultID));
            env.close(std::chrono::seconds{61});
            return std::pair{vaultKeylet, brokerKeylet};
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

        // Under featureLendingProtocolV1_1 the immutability of sfAsset, sfAccount,
        // sfShareMPTID and sfLEVersion is enforced by NoModifiedUnmodifiableFields.
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

        doInvariantCheck(
            {"changed an unchangeable field"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const keylet = keylet::vault(a1.id(), SeqProxy::rawSequence(ac.view().seq()));
                auto sleVault = ac.view().peek(keylet);
                if (!sleVault)
                    return false;
                (*sleVault)[sfLEVersion] = std::to_underlying(VaultVersion::Legacy);
                ac.view().update(sleVault);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&precloseXrp](Account const& a1, Account const& a2, Env& env) {
                return precloseXrp(a1, a2, env, VaultVersion::CashBasis);
            });

        // Pre-featureLendingProtocolV1_1 sfAsset, sfAccount and sfShareMPTID are
        // guarded by ValidVault instead, so both paths need coverage. ValidVault
        // returns early once the result is already tec, hence no escalation to
        // tef on the second pass.
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

        // Loan flags may only change under the transaction types that own
        // those transitions.
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
                auto const keys = createClosedXrpBroker(a1, env);
                if (!keys)
                    continue;
                auto const& brokerKeylet = keys->second;

                OpenView ov{*env.current()};
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

        // ttLOAN_PAY success post-conditions. A loan left with payments still
        // remaining after a successful payment must show that payment in its
        // balance and schedule: PrincipalOutstanding and PaymentRemaining both
        // strictly decrease, and NextPaymentDueDate advances by a positive
        // multiple of PaymentInterval. Each case seeds the same loan, then applies
        // an after-image that breaks exactly one of those conditions.
        {
            struct Case
            {
                Number principal;
                std::uint32_t remaining;
                std::uint32_t dueDate;
                std::string expected;
            };
            auto const cases = std::to_array<Case>({
                {.principal = Number(100),
                 .remaining = 1,
                 .dueDate = 110,
                 .expected = "loan pay must strictly decrease PrincipalOutstanding"},
                {.principal = Number(50),
                 .remaining = 2,
                 .dueDate = 110,
                 .expected = "loan pay must decrease PaymentRemaining"},
                {.principal = Number(50),
                 .remaining = 1,
                 .dueDate = 100,
                 .expected = "loan pay must advance NextPaymentDueDate"},
                // Advanced, but not by a whole number of payment intervals.
                {.principal = Number(50),
                 .remaining = 1,
                 .dueDate = 105,
                 .expected = "loan pay must advance NextPaymentDueDate"},
            });

            for (auto const& c : cases)
            {
                Env env{*this, all_};
                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                auto const keys = createClosedXrpBroker(a1, env);
                if (!keys)
                    continue;
                auto const& brokerKeylet = keys->second;

                OpenView ov{*env.current()};
                auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
                {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(150);
                    sleLoan->at(sfPaymentInterval) = 10u;
                    sleLoan->setFieldU32(sfPaymentRemaining, 2);
                    sleLoan->setFieldU32(sfNextPaymentDueDate, 100);
                    ov.rawInsert(sleLoan);
                }

                STTx const tx{
                    ttLOAN_PAY, [](STObject& t) { t.setFieldAmount(sfAmount, XRPAmount(50)); }};
                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                ApplyContext ac{
                    env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
                CurrentTransactionRulesGuard const rulesGuard(ov.rules());

                auto sleLoan = ac.view().peek(loanKeylet);
                if (!BEAST_EXPECT(sleLoan))
                    continue;
                sleLoan->at(sfPrincipalOutstanding) = c.principal;
                sleLoan->setFieldU32(sfPaymentRemaining, c.remaining);
                sleLoan->setFieldU32(sfNextPaymentDueDate, c.dueDate);
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

                auto const sharesKeylet = keylet::mptokenIssuance(sleVaultRead->at(sfShareMPTID));
                auto const sleSharesRead = ov.read(sharesKeylet);
                if (!BEAST_EXPECT(sleSharesRead))
                    return;
                auto sleShares = std::make_shared<SLE>(*sleSharesRead);
                sleShares->at(sfOutstandingAmount) = 1;
                ov.rawReplace(sleShares);
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

        // A loan may only be deleted by a LoanDelete transaction, and only once
        // it is fully paid off. Both branches are exercised by creating a real
        // loan in the Preclose (so it exists in the base ledger with outstanding
        // principal) and then erasing it in the Precheck.
        {
            Keylet loanKeylet = keylet::amendments();
            auto const precloseLoan = [&loanKeylet, this](
                                          Account const& a1, Account const& a2, Env& env) -> bool {
                PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
                auto const brokerKeylet = createLoanBroker(a1, env, xrpAsset);
                auto const brokerSle = env.le(brokerKeylet);
                if (!BEAST_EXPECT(brokerSle))
                    return false;
                auto const vaultKeylet = keylet::vault(brokerSle->at(sfVaultID));
                Vault const vault{env};
                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = xrpAsset(100)}));
                env.close(std::chrono::seconds{61});

                loanKeylet = keylet::loan(
                    brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
                env(loan::set(a2, brokerKeylet.key, xrpAsset(50).value()),
                    loan::kCounterparty(a1),
                    Sig(sfCounterpartySignature, a1),
                    loan::kPaymentInterval(60),
                    loan::kPaymentTotal(1),
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
        }

        STTx const loanSetTx{
            ttLOAN_SET, [](STObject& tx) { tx.at(sfPrincipalRequested) = Number(0); }};

        // Loan interest due (total value less principal and management fee) must
        // never be negative. The loan below carries a total value short of its
        // principal, while every individual field stays non-negative. A real
        // broker over an XRP vault is created in the preclose, both so the
        // earlier broker-existence checks pass and so the deficit is measured
        // in an integral asset domain, where no rounding tolerance applies.
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
                {"Loan interest due is negative"},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleLoan = makeLoanSle(brokerKeylet.key, 1, a2.id());
                    sleLoan->at(sfPrincipalOutstanding) = Number(100);
                    sleLoan->at(sfTotalValueOutstanding) = Number(90);
                    sleLoan->setFieldU32(sfPaymentRemaining, 1);
                    ac.view().insert(sleLoan);
                    return true;
                },
                XRPAmount{},
                loanSetTx,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseBroker);
        }

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
        // NoModifiedUnmodifiableFields under featureLendingProtocolV1_1.
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

        // featureLendingProtocolV1_1 moves the vault immutability checks from VaultInvariant to
        // InvariantCheck.
        doInvariantCheck(
            makeEnv(all_),
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
        // PaymentInterval * PaymentRemaining + kLoanRedemptionBuffer <= RedemptionDate.
        // LoanSet::preclaim enforces the same bound; this test synthesises a loan whose
        // final payment is still before RedemptionDate (so the old unbuffered check would
        // pass) but inside the buffer zone.
        Keylet closedEndedBrokerKeylet = keylet::amendments();
        std::uint32_t closedEndedRed = 0;
        doInvariantCheck(
            {"closed-ended loan final payment must precede RedemptionDate by at least "
             "kLoanRedemptionBuffer"},
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

                // Final payment at RedemptionDate - (kLoanRedemptionBuffer - 1): still
                // strictly before RedemptionDate, but inside the buffer.
                auto sleLoan = makeLoanSle(closedEndedBrokerKeylet.key, loanSeq, a1.id());
                sleLoan->at(sfLoanBrokerID) = closedEndedBrokerKeylet.key;
                sleLoan->at(sfLoanSequence) = loanSeq;
                sleLoan->at(sfBorrower) = a1.id();
                sleLoan->at(sfStartDate) = closedEndedRed - kLoanRedemptionBuffer;
                sleLoan->at(sfPaymentInterval) = 1;
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

            // Under fixCleanup3_4_0 impair requires the payment to already
            // be late, so advance past the loan's due date first.
            if (env.current()->rules().enabled(fixCleanup3_4_0))
            {
                auto const loanSle = env.le(loanKeylet);
                if (!BEAST_EXPECT(loanSle))
                    return vaultKeylet;
                std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);
                env.close(
                    NetClock::time_point{NetClock::duration{dueDate}} + std::chrono::seconds{1});
            }

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
