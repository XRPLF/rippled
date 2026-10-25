#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
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

#include <array>
#include <format>
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

public:
    void
    run() override
    {
        testAmendmentGate();
        testBatchDisabled();
        testKeylets();
        testCreate();
        testMalformed();
    }
};

BEAST_DEFINE_TESTSUITE(VaultManager, app, xrpl);

}  // namespace xrpl
