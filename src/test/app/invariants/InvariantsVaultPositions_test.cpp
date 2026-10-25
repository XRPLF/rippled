#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsVaultPositions_test : public InvariantsBase
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

    // A closed-ended USD vault in its subscription phase, a manager entry
    // over EQT and BND, and a 100 EQT position opened at 10.
    struct Managed
    {
        jtx::Account gw{"gw"};
        jtx::Account owner{"owner"};
        jtx::Account manager{"manager"};
        jtx::Account valuer{"valuer"};
        jtx::Account guardian{"guardian"};
        jtx::Account alice{"alice"};
        jtx::Account bob{"bob"};
        jtx::Account mm{"mm"};
        jtx::PrettyAsset usd{gw["USD"]};
        jtx::PrettyAsset eq{gw["EQT"]};
        jtx::PrettyAsset bnd{gw["BND"]};
        uint256 vaultID;

        [[nodiscard]] Keylet
        managerKey() const
        {
            return keylet::vaultManager(vaultID);
        }

        [[nodiscard]] Keylet
        positionKey() const
        {
            return keylet::vaultPosition(vaultID, eq);
        }
    };

    static Managed
    setup(jtx::Env& env)
    {
        using namespace jtx;
        using namespace jtx::vault_manager;
        Managed m;
        env.fund(
            XRP(100'000), m.gw, m.owner, m.manager, m.valuer, m.guardian, m.alice, m.bob, m.mm);
        env.close();
        env(fset(m.gw, asfAllowTrustLineClawback));
        for (auto const& a : {m.owner, m.manager, m.valuer, m.guardian, m.alice, m.bob, m.mm})
        {
            env.trust(m.usd(1'000'000), a);
            env.trust(m.eq(1'000'000), a);
        }
        env.close();
        env(pay(m.gw, m.alice, m.usd(100'000)));
        env(pay(m.gw, m.mm, m.usd(500'000)));
        env(pay(m.gw, m.mm, m.eq(100'000)));
        env.close();

        Vault const vault{env};
        auto const sub = env.now().time_since_epoch().count() + 10 * 86'400;
        auto [tx, keylet] = vault.create(
            {.owner = m.owner,
             .asset = m.usd,
             .vaultKind = std::to_underlying(VaultKind::ClosedEnded),
             .subscriptionDate = sub,
             .redemptionDate = sub + 100 * 86'400});
        env(tx);
        env.close();
        m.vaultID = keylet.key;
        env(vault.deposit({.depositor = m.alice, .id = m.vaultID, .amount = m.usd(10'000)}));
        env(offer(m.mm, m.usd(100'000), m.eq(10'000)));
        env.close();

        env(set(m.owner, m.vaultID),
            kManager(m.manager.id()),
            kValuer(m.valuer.id()),
            kGuardian(m.guardian.id()),
            mandate({mandateEntry(m.eq), mandateEntry(m.bnd)}),
            kDeployedMaximum(Number{100'000}),
            kMarkMoveLimit(10'000),
            kMarkHaltPeriod(3'600),
            kMarkStaleAfter(86'400),
            kTimelock(86'400));
        env.close();
        env(trade(m.manager, m.vaultID, m.eq(100), m.usd(1'100)));
        env.close();
        return m;
    }

    static STTx
    makeTx(TxType type, Managed const& m, AccountID const& account)
    {
        return STTx{type, [&](STObject& o) {
                        o.setAccountID(sfAccount, account);
                        o.setFieldH256(sfVaultID, m.vaultID);
                    }};
    }

    // A VaultTrade opening EQT, or closing it when close is set.
    static STTx
    tradeTx(Managed const& m, bool close = false)
    {
        return STTx{ttVAULT_TRADE, [&](STObject& o) {
                        o.setAccountID(sfAccount, m.manager.id());
                        o.setFieldH256(sfVaultID, m.vaultID);
                        o.setFieldAmount(sfAmount, close ? m.usd(100).value() : m.eq(10).value());
                        o.setFieldAmount(sfSendMax, close ? m.eq(10).value() : m.usd(100).value());
                    }};
    }

    using Mutate = std::function<bool(Managed const&, ApplyContext&)>;

    void
    check(
        std::vector<std::string> const& logs,
        STTx (*tx)(Managed const&),
        Mutate const& mutate,
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
        std::function<void(Managed const&, jtx::Env&)> const& more = {},
        std::source_location const& loc = std::source_location::current())
    {
        jtx::Env env{*this, all_};
        auto const m = setup(env);
        if (more)
        {
            more(m, env);
            env.close();
        }
        doInvariantCheck(
            std::move(env),
            m.owner,
            m.manager,
            logs,
            [&](jtx::Account const&, jtx::Account const&, ApplyContext& ac) {
                return mutate(m, ac);
            },
            XRPAmount{},
            tx(m),
            ters,
            loc);
    }

    template <TxType Type>
    static STTx
    byManager(Managed const& m)
    {
        return makeTx(Type, m, m.manager.id());
    }

    template <TxType Type>
    static STTx
    byOwner(Managed const& m)
    {
        return makeTx(Type, m, m.owner.id());
    }

    static STTx
    openTx(Managed const& m)
    {
        return tradeTx(m, false);
    }

    void
    testValidVault()
    {
        testcase("ValidVault managed cases");

        // A trade moves the totals by exactly the vault balance change.
        check(
            {"trade or claim and assets outstanding must add up"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                auto line = ac.view().peek(
                    keylet::trustLine(vault->at(sfAccount), m.usd.raw().get<Issue>()));
                bool const low = AccountID{vault->at(sfAccount)} < m.gw.id();
                auto bal = line->getFieldAmount(sfBalance);
                bal += low ? m.usd(-10).value() : m.usd(10).value();
                line->setFieldAmount(sfBalance, bal);
                ac.view().update(line);
                vault->at(sfAssetsTotal) -= Number{3};
                vault->at(sfAssetsAvailable) -= Number{10};
                ac.view().update(vault);
                return true;
            });

        // A claim never adds to the vault.
        check(
            {"claim must decrease vault balance"},
            &byManager<ttVAULT_MANAGER_CLAIM>,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                auto line = ac.view().peek(
                    keylet::trustLine(vault->at(sfAccount), m.usd.raw().get<Issue>()));
                bool const low = AccountID{vault->at(sfAccount)} < m.gw.id();
                auto bal = line->getFieldAmount(sfBalance);
                bal += low ? m.usd(10).value() : m.usd(-10).value();
                line->setFieldAmount(sfBalance, bal);
                ac.view().update(line);
                vault->at(sfAssetsTotal) += Number{10};
                vault->at(sfAssetsAvailable) += Number{10};
                ac.view().update(vault);
                return true;
            });

        // A mark never changes the totals.
        check(
            {"mark or manager set must not change vault assets"},
            &byManager<ttVAULT_MARK>,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                vault->at(sfAssetsTotal) += Number{1};
                ac.view().update(vault);
                return true;
            });
    }

    void
    testPositions()
    {
        testcase("ValidVaultPositions positions");

        check(
            {"position mark price must be positive"},
            &byManager<ttVAULT_MARK>,
            [](Managed const& m, ApplyContext& ac) {
                auto pos = ac.view().peek(m.positionKey());
                pos->at(sfMarkPrice) = Number{};
                ac.view().update(pos);
                return true;
            });

        check(
            {"position mark time moved backwards"},
            &byManager<ttVAULT_MARK>,
            [](Managed const& m, ApplyContext& ac) {
                auto pos = ac.view().peek(m.positionKey());
                pos->at(sfLastMarkTime) = pos->at(sfLastMarkTime) - 1;
                ac.view().update(pos);
                return true;
            });

        check(
            {"position mark changed outside VaultMark"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto pos = ac.view().peek(m.positionKey());
                pos->at(sfLastMarkTime) = pos->at(sfLastMarkTime) + 1;
                ac.view().update(pos);
                return true;
            });

        check(
            {"position cost basis changed outside VaultTrade"},
            &byManager<ttVAULT_MARK>,
            [](Managed const& m, ApplyContext& ac) {
                auto pos = ac.view().peek(m.positionKey());
                pos->at(sfCostBasis) += Number{1};
                ac.view().update(pos);
                return true;
            });

        check({"position cost basis is negative"}, &openTx, [](Managed const& m, ApplyContext& ac) {
            auto pos = ac.view().peek(m.positionKey());
            pos->at(sfCostBasis) = Number{-1};
            ac.view().update(pos);
            return true;
        });

        check(
            {"position created outside VaultTrade"},
            [](Managed const& m) {
                return STTx{
                    ttACCOUNT_SET, [&](STObject& o) { o.setAccountID(sfAccount, m.owner.id()); }};
            },
            [](Managed const& m, ApplyContext& ac) {
                auto sle = std::make_shared<SLE>(keylet::vaultPosition(m.vaultID, m.bnd));
                sle->at(sfVaultID) = m.vaultID;
                sle->at(sfAsset) = STIssue{sfAsset, Asset{m.bnd.raw()}};
                sle->at(sfMarkPrice) = Number{1};
                sle->at(sfLastMarkTime) = 1;
                sle->at(sfOwnerNode) = 0;
                ac.view().insert(sle);
                return true;
            });

        check(
            {"position deleted outside VaultTrade"},
            &byManager<ttVAULT_MARK>,
            [](Managed const& m, ApplyContext& ac) {
                ac.view().erase(ac.view().peek(m.positionKey()));
                return true;
            });

        check(
            {"position asset is not in the mandate"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                Asset const zzz{m.gw["ZZZ"].issue()};
                auto sle = std::make_shared<SLE>(keylet::vaultPosition(m.vaultID, zzz));
                sle->at(sfVaultID) = m.vaultID;
                sle->at(sfAsset) = STIssue{sfAsset, zzz};
                sle->at(sfMarkPrice) = Number{1};
                sle->at(sfLastMarkTime) = 1;
                sle->at(sfOwnerNode) = 0;
                ac.view().insert(sle);
                return true;
            });

        check(
            {"vault position count does not match its positions"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                vault->at(sfManagedPositionCount) += 1;
                ac.view().update(vault);
                return true;
            });

        check(
            {"trade left a position with no holding"},
            [](Managed const& m) { return tradeTx(m, true); },
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().read(keylet::vault(m.vaultID));
                auto line = ac.view().peek(
                    keylet::trustLine(vault->at(sfAccount), m.eq.raw().get<Issue>()));
                line->setFieldAmount(sfBalance, STAmount{line->getFieldAmount(sfBalance).asset()});
                ac.view().update(line);
                return true;
            });

        check(
            {"open exceeds the deployed maximum"}, &openTx, [](Managed const& m, ApplyContext& ac) {
                auto manager = ac.view().peek(m.managerKey());
                manager->at(sfDeployedMaximum) = Number{1};
                ac.view().update(manager);
                return true;
            });
    }

    void
    testRoles()
    {
        testcase("ValidVaultPositions roles and halts");

        check(
            {"role-holder dealt in the vault"},
            &byManager<ttVAULT_DEPOSIT>,
            [](Managed const&, ApplyContext&) { return true; });

        // A role changes only when its pending change falls due.
        check(
            {"roles changed before the timelock"},
            &byOwner<ttVAULT_MANAGER_SET>,
            [](Managed const& m, ApplyContext& ac) {
                auto manager = ac.view().peek(m.managerKey());
                manager->at(sfManager) = m.bob.id();
                ac.view().update(manager);
                return true;
            },
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [](Managed const& m, jtx::Env& env) {
                env(jtx::vault_manager::set(m.owner, m.vaultID),
                    jtx::vault_manager::kManager(m.bob.id()));
            });

        check(
            {"tightening placed in the pending update"},
            &byOwner<ttVAULT_MANAGER_SET>,
            [](Managed const& m, ApplyContext& ac) {
                auto manager = ac.view().peek(m.managerKey());
                STObject pending = STObject::makeInnerObject(sfPendingUpdate);
                pending[sfDeployedMaximum] = Number{1};
                manager->setFieldObject(sfPendingUpdate, pending);
                manager->at(sfPendingUntil) =
                    ac.view().parentCloseTime().time_since_epoch().count() + 86'400;
                ac.view().update(manager);
                return true;
            });

        check(
            {"vault roles are not disjoint"},
            &byOwner<ttVAULT_MANAGER_SET>,
            [](Managed const& m, ApplyContext& ac) {
                auto manager = ac.view().peek(m.managerKey());
                manager->at(sfValuer) = m.manager.id();
                ac.view().update(manager);
                return true;
            });

        check(
            {"vault halt moved backwards"},
            &byManager<ttVAULT_MARK>,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                vault->at(sfMarkHaltUntil) = vault->at(sfMarkHaltUntil) - 1;
                ac.view().update(vault);
                return true;
            },
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [](Managed const& m, jtx::Env& env) {
                env(jtx::vault_manager::set(m.guardian, m.vaultID, tfHalt));
            });

        check(
            {"vault halt changed outside VaultMark or VaultManagerSet"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                vault->at(sfMarkHaltUntil) = vault->at(sfMarkHaltUntil) + 100;
                ac.view().update(vault);
                return true;
            });
    }

    void
    testUnmodifiable()
    {
        testcase("unmodifiable fields and pseudo-accounts");

        check(
            {"changed an unchangeable field"},
            &byOwner<ttVAULT_MANAGER_SET>,
            [](Managed const& m, ApplyContext& ac) {
                auto manager = ac.view().peek(m.managerKey());
                manager->at(sfVaultID) = uint256{9};
                ac.view().update(manager);
                return true;
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        check(
            {"changed an unchangeable field"},
            &byOwner<ttVAULT_MANAGER_SET>,
            [](Managed const& m, ApplyContext& ac) {
                auto manager = ac.view().peek(m.managerKey());
                manager->at(sfAccount) = m.bob.id();
                ac.view().update(manager);
                return true;
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        check(
            {"changed an unchangeable field"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto pos = ac.view().peek(m.positionKey());
                pos->at(sfVaultID) = uint256{9};
                ac.view().update(pos);
                return true;
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        check(
            {"changed an unchangeable field"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto pos = ac.view().peek(m.positionKey());
                pos->at(sfAsset) = STIssue{sfAsset, Asset{m.bnd.raw()}};
                ac.view().update(pos);
                return true;
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        check(
            {"deleted VaultManager without deleting its pseudo-account"},
            &byOwner<ttVAULT_MANAGER_DELETE>,
            [](Managed const& m, ApplyContext& ac) {
                ac.view().erase(ac.view().peek(m.managerKey()));
                return true;
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
    }

    void
    testAccrualAndHoldings()
    {
        testcase("accrual state and the vault's own holding");

        check(
            {"vault price struck outside a dealing transaction"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                vault->at(sfStruckPrice) = Number{2};
                ac.view().update(vault);
                return true;
            });

        check(
            {"managed vault transaction changed vault accrual state"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().peek(keylet::vault(m.vaultID));
                vault->at(sfUnearnedInterest) = Number{5};
                ac.view().update(vault);
                return true;
            });

        // Only a position holding may go; the vault asset's holding stays.
        check(
            {"vault pseudo-account holding deleted by a non-VaultDelete transaction"},
            &openTx,
            [](Managed const& m, ApplyContext& ac) {
                auto vault = ac.view().read(keylet::vault(m.vaultID));
                ac.view().erase(ac.view().peek(
                    keylet::trustLine(vault->at(sfAccount), m.usd.raw().get<Issue>())));
                return true;
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
    }

public:
    void
    run() override
    {
        testValidVault();
        testPositions();
        testRoles();
        testUnmodifiable();
        testAccrualAndHoldings();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsVaultPositions, app, xrpl);

}  // namespace xrpl::test
