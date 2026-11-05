#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/delegate.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class PerpDelegate_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    Account const dave_{"dave"};

    /**
     * A USD exchange with one XRP/USD market on a single tier of 4 % initial and 2 % maintenance
     * margin, with the oracles at price; dave is funded with XRP only. Closes the ledger.
     */
    struct Trading
    {
        World world;
        Keylet exchange;
        Keylet market;
    };

    Trading
    setup(Env& env, std::uint32_t price = 2)
    {
        using namespace test::jtx;
        auto world = setupMarketWorld(env, {{"XRP", "USD", price * 10, 1}});
        env.fund(XRP(10'000), dave_);
        auto const exchange = createExchange(env, owner_, usd_);

        auto args = marketArgs();
        args.indexMaxLedgers = 1'000;
        args.tiers = {
            {.notionalMax = Number{1'000'000},
             .initialMarginRate = 4'000,
             .maintenanceMarginRate = 2'000}};
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();
        return {
            .world = std::move(world), .exchange = exchange, .market = marketKeylet(exchange.key)};
    }

    [[nodiscard]] static json::Value
    order(
        Account const& account,
        Keylet const& market,
        Number const& price,
        Number const& size,
        std::uint32_t flags = 0)
    {
        auto jv = Perp::order(account, market.key, price, size);
        if (flags != 0)
            jv[jss::Flags] = flags;
        return jv;
    }

    [[nodiscard]] static Keylet
    marginKeylet(Account const& account, Trading const& t)
    {
        return keylet::marginAccount(account.id(), t.exchange.key);
    }

    [[nodiscard]] static Number
    cashOf(Env& env, Account const& account, Trading const& t)
    {
        auto const sle = env.le(marginKeylet(account, t));
        return sle ? Number((*sle)[sfCash]) : Number(-1);
    }

    [[nodiscard]] static Number
    sizeOf(Env& env, Account const& account, Trading const& t)
    {
        auto const sle = env.le(keylet::marginPosition(marginKeylet(account, t).key, t.market.key));
        return sle ? Number((*sle)[sfSize]) : Number();
    }

    [[nodiscard]] static Keylet
    orderAt(Account const& account, std::uint32_t sequence)
    {
        return keylet::perpOrder(account.id(), SeqProxy::rawSequence(sequence));
    }

    void
    testDelegatedTrading()
    {
        testcase("delegated trading");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        env(delegate::set(
            alice_,
            dave_,
            {"MarginDeposit",
             "PerpOrderCreate",
             "PerpOrderCancel",
             "PerpLiquidate",
             "PerpRefresh"}));
        env.close();

        // dave pays the fee; alice pays the deposit.
        auto const aliceXrp = env.balance(alice_);
        auto const daveXrp = env.balance(dave_);
        auto const aliceUsd = env.balance(alice_, usd_);
        env(Perp::deposit(alice_, t.exchange.key, usd_(500)), delegate::As(dave_));
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(500));
        BEAST_EXPECT(env.balance(alice_, usd_) == aliceUsd - usd_(500));
        BEAST_EXPECT(env.balance(alice_) == aliceXrp);
        BEAST_EXPECT(env.balance(dave_) < daveXrp);

        // dave places and cancels an order that alice owns.
        auto const sequence = env.seq(alice_);
        env(order(alice_, t.market, Number{19, -1}, Number{10}), delegate::As(dave_));
        env.close();
        auto const resting = env.le(orderAt(alice_, sequence));
        if (BEAST_EXPECT(resting))
            BEAST_EXPECT((*resting)[sfAccount] == alice_.id());
        env(Perp::cancel(alice_, sequence), delegate::As(dave_));
        env.close();
        BEAST_EXPECT(!env.le(orderAt(alice_, sequence)));

        // dave refreshes the market and settles alice's account.
        env(Perp::refresh(alice_, t.market.key), delegate::As(dave_));
        env(Perp::refreshAccount(alice_, marginKeylet(alice_, t).key), delegate::As(dave_));
        env.close();

        // bob has given dave nothing.
        env(Perp::deposit(bob_, t.exchange.key, usd_(500)),
            delegate::As(dave_),
            Ter(terNO_DELEGATE_PERMISSION));
        env(order(bob_, t.market, Number{19, -1}, Number{10}),
            delegate::As(dave_),
            Ter(terNO_DELEGATE_PERMISSION));
    }

    void
    testNoDelegatedWithdrawal()
    {
        testcase("no delegated withdrawal");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        env(Perp::deposit(alice_, t.exchange.key, usd_(500)));
        env(delegate::set(alice_, dave_, {"MarginDeposit", "PerpOrderCreate", "PerpOrderCancel"}));
        env.close();

        // MarginWithdraw cannot be granted, and preflight refuses a delegated transaction of a
        // type that is not delegable, so a trading key never moves funds out.
        env(delegate::set(alice_, dave_, {"MarginWithdraw"}), Ter(temMALFORMED));
        env(Perp::withdraw(alice_, t.exchange.key, usd_(10)), delegate::As(dave_), Ter(temINVALID));
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(500));

        // Nor can the owner transactions.
        for (auto const* name :
             {"PerpExchangeSet", "PerpExchangeDelete", "PerpMarketSet", "PerpMarketDelete"})
        {
            env(delegate::set(owner_, dave_, {name}), Ter(temMALFORMED));
        }
        env(delegate::set(owner_, dave_, {"PerpRefresh"}));
        env.close();
        env(Perp::exchangeUpdate(owner_, t.exchange.key), delegate::As(dave_), Ter(temINVALID));
        env(Perp::exchangeDelete(owner_, t.exchange.key), delegate::As(dave_), Ter(temINVALID));
        env(Perp::marketUpdate(owner_, t.market.key), delegate::As(dave_), Ter(temINVALID));
        env(Perp::marketDelete(owner_, t.market.key), delegate::As(dave_), Ter(temINVALID));
        env(Perp::withdraw(owner_, t.exchange.key, usd_(10)),
            Txflags(tfPerpOwnerFees),
            delegate::As(dave_),
            Ter(temINVALID));
    }

    void
    testDelegatedLiquidate()
    {
        testcase("delegated liquidate");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env, 100);
        env(Perp::deposit(alice_, t.exchange.key, usd_(1'200)));
        env(Perp::deposit(bob_, t.exchange.key, usd_(2'000)));
        env(Perp::deposit(liq_, t.exchange.key, usd_(1'000)));
        env(delegate::set(liq_, dave_, {"PerpLiquidate"}));
        env.close();

        // alice short 100 at 100; at 110 her equity 200 is below MM 220, so 20 % is taken.
        env(order(bob_, t.market, Number{100}, Number{100}));
        env.close();
        env(order(alice_, t.market, Number{100}, Number{100}, tfPerpSell));
        env.close();
        for (auto& oracle : t.world.oracles)
            oracle.set(test::jtx::oracle::UpdateArg{.series = {{"XRP", "USD", 1'100, 1}}});
        env.close();

        env(Perp::liquidate(
                liq_, marginKeylet(alice_, t).key, t.market.key, tfPerpLiquidateTakeover),
            delegate::As(dave_));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(-80));
        BEAST_EXPECT(sizeOf(env, liq_, t) == Number(-20));
        BEAST_EXPECT(near(cashOf(env, liq_, t), Number(1'011)));
    }

    /**
     * Rests alice's two bids and two asks around index 2; returns their sequences.
     */
    std::vector<std::uint32_t>
    restQuotes(Env& env, Trading const& t) const
    {
        std::vector<std::uint32_t> sequences;
        for (auto const& [price, flags] : std::vector<std::pair<Number, std::uint32_t>>{
                 {Number{190, -2}, 0},
                 {Number{191, -2}, 0},
                 {Number{209, -2}, tfPerpSell},
                 {Number{210, -2}, tfPerpSell}})
        {
            sequences.push_back(env.seq(alice_));
            env(order(alice_, t.market, price, Number{10}, flags));
        }
        env.close();
        return sequences;
    }

    void
    testBatchCancelReplace()
    {
        testcase("batch cancel and replace");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
        env.close();
        auto const old = restQuotes(env, t);

        // Eight inner transactions: four cancels, then the four replacements one tick closer.
        auto const replace = [&](bool failLast) {
            std::vector<json::Value> inners;
            inners.reserve(old.size() + 4);
            for (auto const sequence : old)
                inners.push_back(Perp::cancel(alice_, sequence));
            inners.push_back(order(alice_, t.market, Number{192, -2}, Number{10}));
            inners.push_back(order(alice_, t.market, Number{193, -2}, Number{10}));
            inners.push_back(order(alice_, t.market, Number{207, -2}, Number{10}, tfPerpSell));
            // A bid above the order band fails the last inner when asked to.
            inners.push_back(
                failLast ? order(alice_, t.market, Number{3}, Number{10})
                         : order(alice_, t.market, Number{208, -2}, Number{10}, tfPerpSell));
            return inners;
        };

        // All or nothing: a failing replacement leaves every old order in place.
        {
            auto const seq = env.seq(alice_);
            auto const inners = replace(true);
            env(batch::outer(alice_, seq, batch::calcBatchFee(env, 0, 8), tfAllOrNothing),
                batch::Inner(inners[0], seq + 1),
                batch::Inner(inners[1], seq + 2),
                batch::Inner(inners[2], seq + 3),
                batch::Inner(inners[3], seq + 4),
                batch::Inner(inners[4], seq + 5),
                batch::Inner(inners[5], seq + 6),
                batch::Inner(inners[6], seq + 7),
                batch::Inner(inners[7], seq + 8));
            env.close();
            for (auto const sequence : old)
                BEAST_EXPECT(env.le(orderAt(alice_, sequence)));
            BEAST_EXPECT(!env.le(orderAt(alice_, seq + 5)));
        }

        {
            auto const seq = env.seq(alice_);
            auto const inners = replace(false);
            env(batch::outer(alice_, seq, batch::calcBatchFee(env, 0, 8), tfAllOrNothing),
                batch::Inner(inners[0], seq + 1),
                batch::Inner(inners[1], seq + 2),
                batch::Inner(inners[2], seq + 3),
                batch::Inner(inners[3], seq + 4),
                batch::Inner(inners[4], seq + 5),
                batch::Inner(inners[5], seq + 6),
                batch::Inner(inners[6], seq + 7),
                batch::Inner(inners[7], seq + 8));
            env.close();
            for (auto const sequence : old)
                BEAST_EXPECT(!env.le(orderAt(alice_, sequence)));
            for (std::uint32_t i = 5; i <= 8; ++i)
                BEAST_EXPECT(env.le(orderAt(alice_, seq + i)));
            BEAST_EXPECT(perp::bestPrice(*env.closed(), t.market.key, false) == Number(193, -2));
            BEAST_EXPECT(perp::bestPrice(*env.closed(), t.market.key, true) == Number(207, -2));
        }
    }

    void
    testBatchDepositThenOrder()
    {
        testcase("batch deposit then order");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);

        // carol has no margin account until the first inner creates it.
        auto const seq = env.seq(carol_);
        env(batch::outer(carol_, seq, batch::calcBatchFee(env, 0, 2), tfAllOrNothing),
            batch::Inner(Perp::deposit(carol_, t.exchange.key, usd_(100)), seq + 1),
            batch::Inner(order(carol_, t.market, Number{195, -2}, Number{10}), seq + 2));
        env.close();
        BEAST_EXPECT(cashOf(env, carol_, t) == Number(100));
        BEAST_EXPECT(env.le(orderAt(carol_, seq + 2)));
    }

public:
    void
    run() override
    {
        testDelegatedTrading();
        testNoDelegatedWithdrawal();
        testDelegatedLiquidate();
        testBatchCancelReplace();
        testBatchDepositThenOrder();
    }
};

BEAST_DEFINE_TESTSUITE(PerpDelegate, perp, xrpl);

}  // namespace xrpl
