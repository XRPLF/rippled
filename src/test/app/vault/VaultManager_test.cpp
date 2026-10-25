#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/batch.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <source_location>
#include <vector>

namespace xrpl {

class VaultManager_test : public VaultManagerTestBase
{
    // One transaction of each new type, against a vault id that need not exist.
    static std::vector<json::Value>
    oneOfEach(test::jtx::Account const& a, uint256 const& id, Asset const& asset)
    {
        using namespace test::jtx::vault_manager;
        STAmount const amt{asset, 1};
        return {
            set(a, id),
            trade(a, id, amt, amt),
            mark(a, id, asset, Number{1}),
            cover(a, id, amt, tfCoverDeposit),
            claim(a, id),
            del(a, id)};
    }

    void
    testAmendmentGate()
    {
        testcase("amendment gate");
        using namespace test::jtx;

        Env env{*this, all_ - featureManagedVault};
        Account const alice{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        for (auto const& tx : oneOfEach(alice, uint256{1}, xrpIssue()))
            env(tx, Ter{temDISABLED});
    }

    void
    testBatchDisabled()
    {
        testcase("batch disabled");
        using namespace test::jtx;

        Env env{*this, all_};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        for (auto const& tx : oneOfEach(alice, uint256{1}, xrpIssue()))
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(tx, seq + 2),
                Ter{temINVALID_INNER_BATCH});
            env.close();
        }
    }

    void
    testKeylets()
    {
        testcase("keylets");

        Issue const usd{Currency{1}, AccountID{2}};
        Issue const eq{Currency{3}, AccountID{2}};
        uint256 const id{5};
        uint256 const id2{6};

        BEAST_EXPECT(keylet::vaultManager(id).type == ltVAULT_MANAGER);
        BEAST_EXPECT(keylet::vaultManager(id).key != keylet::vaultManager(id2).key);
        BEAST_EXPECT(keylet::vaultManager(id).key != id);
        BEAST_EXPECT(keylet::vaultPosition(id, usd).type == ltVAULT_POSITION);
        BEAST_EXPECT(keylet::vaultPosition(id, usd).key != keylet::vaultPosition(id, eq).key);
        BEAST_EXPECT(keylet::vaultPosition(id, usd).key != keylet::vaultPosition(id2, usd).key);
        BEAST_EXPECT(
            keylet::vaultPosition(id, usd).key != keylet::vaultPosition(id, xrpIssue()).key);
    }

    static FundArgs
    noManager(VaultKind kind = VaultKind::ClosedEnded)
    {
        return FundArgs{.kind = kind, .createManager = false};
    }

    void
    testCreate()
    {
        testcase("create");
        using namespace test::jtx;

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager());
            auto const ownerCount = env.le(keylet::account(f.owner))->at(sfOwnerCount);
            env(defaultManager(env, f));
            env.close();

            auto const sle = env.le(managerKeylet(f));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->at(sfVaultID) == f.vaultID());
            BEAST_EXPECT(sle->at(sfManager) == f.manager.id());
            BEAST_EXPECT(sle->at(sfValuer) == f.valuer.id());
            BEAST_EXPECT(sle->at(sfGuardian) == f.guardian.id());
            BEAST_EXPECT(sle->getFieldArray(sfMandate).size() == 1);
            BEAST_EXPECT(sle->getFieldArray(sfMandate)[0][sfAsset] == Asset{f.eq.raw()});
            BEAST_EXPECT(sle->at(sfDeployedMaximum) == Number{100'000});
            BEAST_EXPECT(sle->at(sfMarkMoveLimit) == kMoveLimit);
            BEAST_EXPECT(sle->at(sfMarkHaltPeriod) == kHaltPeriod);
            BEAST_EXPECT(sle->at(sfMarkStaleAfter) == kStaleAfter);
            BEAST_EXPECT(sle->at(sfTimelock) == kTimelockSeconds);
            BEAST_EXPECT(
                sle->at(sfLastFeeTime) ==
                env.closed()->header().parentCloseTime.time_since_epoch().count());
            BEAST_EXPECT(!sle->isFieldPresent(sfPendingUpdate));
            BEAST_EXPECT(env.le(keylet::account(f.owner))->at(sfOwnerCount) == ownerCount + 2);

            AccountID const pseudo = sle->at(sfAccount);
            auto const pseudoSle = env.le(keylet::account(pseudo));
            if (!BEAST_EXPECT(pseudoSle))
                return;
            BEAST_EXPECT(pseudoSle->at(~sfVaultManagerID) == sle->key());
            BEAST_EXPECT(env.le(keylet::trustLine(pseudo, f.usd.raw().get<Issue>())));
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager(VaultKind::Rolling));
            env(defaultManager(env, f));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f)));
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager(VaultKind::OpenEnded));
            env(defaultManager(env, f), Ter{tecNO_PERMISSION});
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager());

            auto jt = defaultManager(env, f);
            jt.jv[sfAccount] = f.alice.human();
            env(jt.jv, Ter{tecNO_PERMISSION});

            jt = defaultManager(env, f);
            jt.jv[sfVaultID] = to_string(uint256{7});
            env(jt.jv, Ter{tecNO_ENTRY});

            for (auto const* field : std::array<SField const*, 9>{
                     &sfManager,
                     &sfValuer,
                     &sfGuardian,
                     &sfMandate,
                     &sfDeployedMaximum,
                     &sfMarkMoveLimit,
                     &sfMarkHaltPeriod,
                     &sfMarkStaleAfter,
                     &sfTimelock})
            {
                auto missing = defaultManager(env, f);
                missing.jv.removeMember(field->jsonName);
                env(missing.jv, Ter{temMALFORMED});
            }
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager());
            auto const fee = env.current()->fees().base;
            auto const ownerCount = env.le(keylet::account(f.owner))->at(sfOwnerCount);
            auto const reserve = env.current()->fees().accountReserve(ownerCount + 1, 1);
            auto const balance = env.balance(f.owner, xrpIssue()).value().xrp();
            env(pay(f.owner, f.gw, STAmount{balance - reserve - fee - fee}));
            env.close();
            env(defaultManager(env, f), Ter{tecINSUFFICIENT_RESERVE});
        }
    }

    void
    testMalformed()
    {
        testcase("malformed");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, noManager());

        auto const withField = [&](SField const& field, json::Value value, TER expected) {
            auto jt = defaultManager(env, f);
            jt.jv[field.jsonName] = std::move(value);
            env(jt.jv, Ter{expected});
        };
        auto const withMandate = [&](std::vector<json::Value> entries, TER expected) {
            json::Value arr{json::ValueType::Array};
            for (auto const& e : entries)
                arr.append(e);
            withField(sfMandate, arr, expected);
        };

        env(set(f.guardian, f.vaultID(), tfVetoPending | tfHalt), Ter{temINVALID_FLAG});
        env(set(f.guardian, f.vaultID(), tfHalt), kDeployedMaximum(Number{1}), Ter{temMALFORMED});

        withMandate({}, temMALFORMED);
        {
            std::vector<json::Value> seventeen;
            for (int i = 0; i < 17; ++i)
                seventeen.push_back(mandateEntry(f.gw[std::format("Q{:02}", i)]));
            withMandate(seventeen, temMALFORMED);
        }
        withMandate({mandateEntry(f.eq), mandateEntry(f.eq)}, temMALFORMED);
        withMandate({mandateEntry(f.eq, Number{}, std::nullopt, 1)}, temMALFORMED);
        withMandate({mandateEntry(f.eq, Number{-1})}, temMALFORMED);
        withMandate({mandateEntry(f.eq, Number{}, uint256{})}, temMALFORMED);
        withMandate({mandateEntry(f.usd)}, tecWRONG_ASSET);

        withField(sfMarkMoveLimit, 0, temMALFORMED);
        withField(sfMarkMoveLimit, 100'001, temMALFORMED);
        withField(sfMarkHaltPeriod, 0, temMALFORMED);
        withField(sfMarkStaleAfter, 0, temMALFORMED);
        withField(sfTimelock, 0, temMALFORMED);
        withField(sfManagementFeeRate, 10'001, temMALFORMED);
        withField(sfPerformanceFeeRate, 100'001, temMALFORMED);
        withField(sfCoverRateMinimum, 100'001, temMALFORMED);
        withField(sfCoverRateLiquidation, 100'001, temMALFORMED);
        withField(sfDeployedMaximum, "-1", temMALFORMED);

        Account const ghost{"ghost"};
        withMandate({mandateEntry(ghost["EQT"])}, tecNO_ISSUER);
        withMandate({mandateEntry(MPTIssue{makeMptID(1, ghost.id())})}, tecOBJECT_NOT_FOUND);
    }

    void
    testRoleConflict()
    {
        testcase("role conflict");
        using namespace test::jtx;

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager());
            auto const withRoles =
                [&](Account const& m, Account const& v, Account const& g, TER expected) {
                    auto jt = defaultManager(env, f);
                    jt.jv[sfManager] = m.human();
                    jt.jv[sfValuer] = v.human();
                    jt.jv[sfGuardian] = g.human();
                    env(jt.jv, Ter{expected});
                };
            withRoles(f.manager, f.manager, f.guardian, tecROLE_CONFLICT);
            withRoles(f.manager, f.valuer, f.manager, tecROLE_CONFLICT);
            withRoles(f.manager, f.valuer, f.valuer, tecROLE_CONFLICT);
            withRoles(f.manager, f.owner, f.guardian, tecROLE_CONFLICT);
            withRoles(f.manager, f.valuer, f.owner, tecROLE_CONFLICT);
            // A sponsor running its own fund: the owner manages.
            withRoles(f.owner, f.valuer, f.guardian, tesSUCCESS);
            env.close();
            // A loan broker's owner is the vault owner, who now manages.
            env(loan_broker::set(f.owner, f.vaultID()), Ter{tecROLE_CONFLICT});
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env, noManager());
            env(loan_broker::set(f.owner, f.vaultID()));
            env.close();
            auto jt = defaultManager(env, f);
            jt.jv[sfManager] = f.owner.human();
            env(jt.jv, Ter{tecROLE_CONFLICT});
            env(defaultManager(env, f));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f)));
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env);
            env(loan_broker::set(f.owner, f.vaultID()));
            env.close();
        }
    }

    // A manager entry with every limit set, so each field can move both ways.
    // EQT carries a position maximum; BND carries a domain.
    void
    createFullManager(test::jtx::Env& env, Fund const& f, uint256 const& domain)
    {
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;
        env(set(f.owner, f.vaultID()),
            kManager(f.manager.id()),
            kValuer(f.valuer.id()),
            kGuardian(f.guardian.id()),
            mandate(
                {mandateEntry(f.eq, Number{1'000}), mandateEntry(f.gw["BND"], Number{}, domain)}),
            kDeployedMaximum(Number{100'000}),
            kCoverRateMinimum(1'000),
            kCoverRateLiquidation(1'000),
            kManagementFeeRate(100),
            kPerformanceFeeRate(1'000),
            kMarkMoveLimit(kMoveLimit),
            kMarkHaltPeriod(kHaltPeriod),
            kMarkStaleAfter(kStaleAfter),
            kTimelock(kTimelockSeconds));
        env.close();
    }

    void
    testTightenLoosen()
    {
        testcase("tighten now, loosen later");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, noManager());
        uint256 const domain{0xD0};
        createFullManager(env, f, domain);
        auto const bnd = f.gw["BND"];

        auto const entry = [&] { return env.le(managerKeylet(f)); };
        auto const propose =
            [&](SField const& field, json::Value value, TER expected = tesSUCCESS) {
                auto jv = set(f.owner, f.vaultID());
                jv[field.jsonName] = std::move(value);
                env(jv, Ter{expected});
                env.close();
            };
        auto const mandateJson = [](std::vector<json::Value> entries) {
            json::Value arr{json::ValueType::Array};
            for (auto const& e : entries)
                arr.append(e);
            return arr;
        };

        // Each tightening applies in the transaction.
        propose(sfDeployedMaximum, "50000");
        propose(sfMarkStaleAfter, 43'200);
        propose(sfMarkMoveLimit, 5'000);
        propose(sfMarkHaltPeriod, 7'200);
        propose(sfCoverRateMinimum, 2'000);
        propose(sfCoverRateLiquidation, 2'000);
        propose(sfManagementFeeRate, 50);
        propose(sfPerformanceFeeRate, 500);
        propose(sfTimelock, 172'800);
        // Lower EQT's cap, add a domain to EQT, drop nothing.
        propose(
            sfMandate,
            mandateJson(
                {mandateEntry(f.eq, Number{500}, domain), mandateEntry(bnd, Number{}, domain)}));
        {
            auto const sle = entry();
            BEAST_EXPECT(!sle->isFieldPresent(sfPendingUpdate));
            BEAST_EXPECT(sle->at(sfPendingUntil) == 0);
            BEAST_EXPECT(sle->at(sfDeployedMaximum) == Number{50'000});
            BEAST_EXPECT(sle->at(sfMarkStaleAfter) == 43'200);
            BEAST_EXPECT(sle->at(sfMarkMoveLimit) == 5'000);
            BEAST_EXPECT(sle->at(sfMarkHaltPeriod) == 7'200);
            BEAST_EXPECT(sle->at(sfCoverRateMinimum) == 2'000);
            BEAST_EXPECT(sle->at(sfCoverRateLiquidation) == 2'000);
            BEAST_EXPECT(sle->at(sfManagementFeeRate) == 50);
            BEAST_EXPECT(sle->at(sfPerformanceFeeRate) == 500);
            BEAST_EXPECT(sle->at(sfTimelock) == 172'800);
            auto const& m = sle->getFieldArray(sfMandate);
            BEAST_EXPECT(m.size() == 2);
            BEAST_EXPECT(m[0][sfPositionMaximum] == Number{500});
            BEAST_EXPECT(m[0][~sfDomainID] == domain);
        }
        // Removing an entry without a position is a tightening.
        propose(sfMandate, mandateJson({mandateEntry(f.eq, Number{500}, domain)}));
        BEAST_EXPECT(entry()->getFieldArray(sfMandate).size() == 1);
        BEAST_EXPECT(!entry()->isFieldPresent(sfPendingUpdate));

        // Each loosening or role change waits; each replaces the last.
        auto const expectPending = [&](SField const& field,
                                       std::source_location const loc =
                                           std::source_location::current()) {
            auto const sle = entry();
            expect(sle->isFieldPresent(sfPendingUpdate), "pending", loc.file_name(), loc.line());
            if (!sle->isFieldPresent(sfPendingUpdate))
                return;
            auto const& pending = sle->getFieldObject(sfPendingUpdate);
            expect(pending.isFieldPresent(field), field.getName(), loc.file_name(), loc.line());
            auto const present = std::ranges::count_if(
                std::array<SField const*, 13>{
                    &sfManager,
                    &sfValuer,
                    &sfGuardian,
                    &sfMandate,
                    &sfDeployedMaximum,
                    &sfCoverRateMinimum,
                    &sfCoverRateLiquidation,
                    &sfManagementFeeRate,
                    &sfPerformanceFeeRate,
                    &sfMarkMoveLimit,
                    &sfMarkHaltPeriod,
                    &sfMarkStaleAfter,
                    &sfTimelock},
                [&](SField const* f) { return pending.isFieldPresent(*f); });
            expect(present == 1, "one field", loc.file_name(), loc.line());
            expect(
                sle->at(sfPendingUntil) ==
                    env.closed()->header().parentCloseTime.time_since_epoch().count() +
                        sle->at(sfTimelock),
                "until",
                loc.file_name(),
                loc.line());
        };
        propose(sfDeployedMaximum, "60000");
        expectPending(sfDeployedMaximum);
        BEAST_EXPECT(entry()->at(sfDeployedMaximum) == Number{50'000});
        propose(sfMarkStaleAfter, 50'000);
        expectPending(sfMarkStaleAfter);
        BEAST_EXPECT(entry()->at(sfMarkStaleAfter) == 43'200);
        propose(sfMarkMoveLimit, 6'000);
        expectPending(sfMarkMoveLimit);
        propose(sfMarkHaltPeriod, 3'600);
        expectPending(sfMarkHaltPeriod);
        propose(sfCoverRateMinimum, 1'000);
        expectPending(sfCoverRateMinimum);
        propose(sfCoverRateLiquidation, 1'000);
        expectPending(sfCoverRateLiquidation);
        propose(sfManagementFeeRate, 60);
        expectPending(sfManagementFeeRate);
        propose(sfPerformanceFeeRate, 600);
        expectPending(sfPerformanceFeeRate);
        propose(sfTimelock, 100'000);
        expectPending(sfTimelock);
        BEAST_EXPECT(entry()->at(sfTimelock) == 172'800);
        propose(sfManager, f.bob.human());
        expectPending(sfManager);
        BEAST_EXPECT(entry()->at(sfManager) == f.manager.id());
        propose(sfValuer, f.bob.human());
        expectPending(sfValuer);
        propose(sfGuardian, f.bob.human());
        expectPending(sfGuardian);
        // Adding an entry, raising a cap, lifting a cap, removing a domain.
        propose(
            sfMandate, mandateJson({mandateEntry(f.eq, Number{500}, domain), mandateEntry(bnd)}));
        expectPending(sfMandate);
        propose(sfMandate, mandateJson({mandateEntry(f.eq, Number{900}, domain)}));
        expectPending(sfMandate);
        propose(sfMandate, mandateJson({mandateEntry(f.eq, Number{}, domain)}));
        expectPending(sfMandate);
        propose(sfMandate, mandateJson({mandateEntry(f.eq, Number{500})}));
        expectPending(sfMandate);
        BEAST_EXPECT(entry()->getFieldArray(sfMandate).size() == 1);
        BEAST_EXPECT(entry()->getFieldArray(sfMandate)[0][~sfDomainID] == domain);

        // A tightening mandate applies now and drops the pending mandate.
        propose(sfMandate, mandateJson({mandateEntry(f.eq, Number{400}, domain)}));
        BEAST_EXPECT(entry()->getFieldArray(sfMandate)[0][sfPositionMaximum] == Number{400});
        BEAST_EXPECT(
            !entry()->isFieldPresent(sfPendingUpdate) ||
            !entry()->getFieldObject(sfPendingUpdate).isFieldPresent(sfMandate));

        // A mixed proposal: the tightening applies, the loosening waits.
        {
            auto jv = set(f.owner, f.vaultID());
            jv[sfDeployedMaximum.jsonName] = "40000";
            jv[sfMarkStaleAfter.jsonName] = 80'000;
            env(jv);
            env.close();
            BEAST_EXPECT(entry()->at(sfDeployedMaximum) == Number{40'000});
            expectPending(sfMarkStaleAfter);
        }

        // Replace and restart: the second proposal's clock starts at its ledger.
        propose(sfDeployedMaximum, "70000");
        auto const firstUntil = entry()->at(sfPendingUntil);
        env.close(std::chrono::seconds{1'000});
        propose(sfDeployedMaximum, "80000");
        auto const until = entry()->at(sfPendingUntil);
        BEAST_EXPECT(until > firstUntil);
        BEAST_EXPECT(entry()->getFieldObject(sfPendingUpdate)[sfDeployedMaximum] == Number{80'000});

        // Lazy application: not before sfPendingUntil, then on the first touch.
        auto const parent = [&] {
            return env.current()->parentCloseTime().time_since_epoch().count();
        };
        env.close(tp{d{until}} - std::chrono::seconds{30});
        BEAST_EXPECT(parent() < until);
        env(set(f.owner, f.vaultID()));
        env.close();
        BEAST_EXPECT(entry()->isFieldPresent(sfPendingUpdate));
        BEAST_EXPECT(entry()->at(sfDeployedMaximum) == Number{40'000});
        env.close(tp{d{until}});
        BEAST_EXPECT(parent() >= until);
        env(set(f.owner, f.vaultID()));
        env.close();
        BEAST_EXPECT(!entry()->isFieldPresent(sfPendingUpdate));
        BEAST_EXPECT(entry()->at(sfPendingUntil) == 0);
        BEAST_EXPECT(entry()->at(sfDeployedMaximum) == Number{80'000});

        // A pending role change making the owner manager blocks a loan broker.
        propose(sfManager, f.owner.human());
        env(loan_broker::set(f.owner, f.vaultID()), Ter{tecROLE_CONFLICT});
    }

public:
    void
    run() override
    {
        testAmendmentGate();
        testBatchDisabled();
        testKeylets();
        testCreate();
        testMalformed();
        testRoleConflict();
        testTightenLoosen();
    }
};

BEAST_DEFINE_TESTSUITE(VaultManager, app, xrpl);

}  // namespace xrpl
