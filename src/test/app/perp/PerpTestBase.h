#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/perp.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/UintTypes.h>

#include <functional>
#include <string>
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
     * Funds every account of the fixture, gives the owner and the traders USD, and moves the
     * close time to the oracle test start time so that OracleSet accepts its default timestamp.
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
        env.close(env.timeKeeper().now() + (oracle::kTestStartTime - kEpochOffset));
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

    /**
     * Creates the owner's exchange in asset with the minimum parameter delay; closes the ledger.
     */
    static Keylet
    createExchange(Env& env, Account const& owner, Asset const& asset)
    {
        auto const exchange = test::jtx::Perp::exchangeKeylet(env, owner);
        env(test::jtx::Perp::exchangeSet(owner, asset));
        env.close();
        return exchange;
    }

    /**
     * The default jtx market on XRP/USD, reading document 1 of oracles o1, o2 and o3.
     */
    [[nodiscard]] test::jtx::Perp::MarketArgs
    marketArgs() const
    {
        test::jtx::Perp::MarketArgs args;
        args.oracles = {
            {.owner = o1_.id(), .documentID = 1},
            {.owner = o2_.id(), .documentID = 1},
            {.owner = o3_.id(), .documentID = 1}};
        return args;
    }

    [[nodiscard]] static Keylet
    marketKeylet(
        uint256 const& exchangeID,
        std::string const& base = "XRP",
        std::string const& quote = "USD")
    {
        return keylet::perpMarket(exchangeID, toCurrency(base), toCurrency(quote));
    }

    /**
     * Applies change to the entry at keylet in the open ledger only, so the next close drops it.
     *
     * A test that seeds state this way submits and checks its transaction before closing.
     */
    void
    seed(
        Env& env,
        Keylet const& keylet,
        std::function<void(ApplyView&, SLE::pointer const&)> const& change)
    {
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            Sandbox sb(&view, TapNone);
            auto const sle = sb.peek(keylet);
            if (!BEAST_EXPECT(sle))
                return false;
            change(sb, sle);
            sb.update(sle);
            sb.apply(view);
            return true;
        });
    }
};

}  // namespace xrpl
