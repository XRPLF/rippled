#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <vector>

namespace xrpl {

/**
 * Shared base for the Perp*_test suites, all registered in module `perp`.
 *
 * Run every suite of the family with `xrpld -u xrpl.perp`.
 */
class PerpTestBase : public beast::unit_test::Suite
{
protected:
    using Env = test::jtx::Env;
    using Account = test::jtx::Account;
    using Oracle = test::jtx::oracle::Oracle;

    FeatureBitset const all_{test::jtx::testableAmendments()};

    Account const owner_{"owner"};
    Account const alice_{"alice"};
    Account const bob_{"bob"};
    Account const carol_{"carol"};
    Account const liq_{"liq"};
    Account const gw_{"gw"};
    Account const o1_{"o1"};
    Account const o2_{"o2"};
    Account const o3_{"o3"};
    test::jtx::IOU const usd_{gw_["USD"]};

    /**
     * The oracles and the backstop vault that setupMarketWorld creates.
     */
    struct World
    {
        std::vector<Oracle> oracles;
        Keylet vault;
    };

    /**
     * Funds every account of the fixture and gives the owner and the traders USD.
     */
    void
    fundAccounts(Env& env) const
    {
        using namespace test::jtx;
        env.fund(XRP(100'000), owner_, alice_, bob_, carol_, liq_, gw_, o1_, o2_, o3_);
        env.close();
        for (auto const& account : {owner_, alice_, bob_, carol_, liq_})
        {
            env.trust(usd_(1'000'000), account);
            env(pay(gw_, account, usd_(100'000)));
        }
        env.close();
    }

    /**
     * Funds the accounts, writes oracles o1, o2 and o3 quoting XRP/USD at 2.0,
     * and creates the owner's USD vault.
     */
    World
    setupMarketWorld(Env& env) const
    {
        using namespace test::jtx;
        fundAccounts(env);

        World world{.oracles = {}, .vault = keylet::unchecked(uint256{})};
        for (auto const& provider : {o1_, o2_, o3_})
        {
            world.oracles.emplace_back(
                env,
                oracle::CreateArg{
                    .owner = provider.id(), .documentID = 1, .series = {{"XRP", "USD", 20, 1}}});
        }
        env.close();

        Vault const vault{.env = env};
        auto [tx, keylet] = vault.create({.owner = owner_, .asset = usd_});
        env(tx);
        env.close();
        world.vault = keylet;
        return world;
    }
};

}  // namespace xrpl
