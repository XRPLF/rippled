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

public:
    void
    run() override
    {
        testAmendmentGate();
        testBatchDisabled();
        testKeylets();
    }
};

BEAST_DEFINE_TESTSUITE(VaultManager, app, xrpl);

}  // namespace xrpl
