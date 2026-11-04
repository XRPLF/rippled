#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/perp.h>
#include <test/jtx/txflags.h>

#include <xrpld/app/ledger/OpenLedger.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class PerpRPC_test : public beast::unit_test::Suite
{
    using Env = test::jtx::Env;
    using Account = test::jtx::Account;
    using Perp = test::jtx::Perp;
    using Oracle = test::jtx::oracle::Oracle;

    Account const owner_{"owner"};
    Account const alice_{"alice"};
    Account const bob_{"bob"};
    Account const carol_{"carol"};
    Account const gw_{"gw"};
    Account const o1_{"o1"};
    Account const o2_{"o2"};
    Account const o3_{"o3"};
    test::jtx::IOU const usd_{gw_["USD"]};

    /**
     * A USD exchange with a cover rate minimum of 10 % and 1000 USD of cover, and its XRP/USD
     * market on three oracles; alice, bob and carol each deposit 1000 USD.
     */
    struct Trading
    {
        std::vector<Oracle> oracles;
        Keylet exchange;
        Keylet market;
    };

    Trading
    setup(Env& env)
    {
        using namespace test::jtx;
        env.fund(XRP(100'000), owner_, alice_, bob_, carol_, gw_, o1_, o2_, o3_);
        env.close();
        for (auto const& account : {owner_, alice_, bob_, carol_})
        {
            env.trust(usd_(1'000'000), account);
            env(pay(gw_, account, usd_(100'000)));
        }
        env.close(env.timeKeeper().now() + (oracle::kTestStartTime - kEpochOffset));

        std::vector<Oracle> oracles;
        for (auto const& provider : {o1_, o2_, o3_})
        {
            oracles.emplace_back(
                env,
                oracle::CreateArg{
                    .owner = provider.id(), .documentID = 1, .series = {{"XRP", "USD", 20, 1}}});
        }
        env.close();

        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        create[sfCoverRateMinimum] = 10'000;
        env(create);
        env.close();

        Perp::MarketArgs args;
        args.oracles = {
            {.owner = o1_.id(), .documentID = 1},
            {.owner = o2_.id(), .documentID = 1},
            {.owner = o3_.id(), .documentID = 1}};
        args.indexMaxLedgers = 1'000;
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();

        for (auto const* trader : {&alice_, &bob_, &carol_})
            env(Perp::deposit(*trader, exchange.key, usd_(1'000)));
        env(Perp::deposit(owner_, exchange.key, usd_(1'000)), Txflags(tfPerpCover));
        env.close();
        return {
            .oracles = std::move(oracles),
            .exchange = exchange,
            .market = keylet::perpMarket(exchange.key, toCurrency("XRP"), toCurrency("USD"))};
    }

    /**
     * bob rests an ask of 4000 at 2 and alice takes it: alice long 4000, bob short 4000.
     */
    void
    trade(Env& env, Trading const& t) const
    {
        using namespace test::jtx;
        env(Perp::order(bob_, t.market.key, Number{2}, Number{4'000}), Txflags(tfPerpSell));
        env.close();
        env(Perp::order(alice_, t.market.key, Number{2}, Number{4'000}));
        env.close();
    }

    /**
     * Moves every oracle to mantissa x 10^-scale; closes the ledger.
     */
    static void
    movePrice(Env& env, Trading& t, std::uint32_t mantissa, std::uint8_t scale)
    {
        for (auto& oracle : t.oracles)
            oracle.set(test::jtx::oracle::UpdateArg{.series = {{"XRP", "USD", mantissa, scale}}});
        env.close();
    }

    [[nodiscard]] static json::Value
    rpc(Env& env, std::string const& method, json::Value const& params)
    {
        return env.rpc("json", method, to_string(params))[jss::result];
    }

    [[nodiscard]] static Keylet
    marginKeylet(Account const& account, Trading const& t)
    {
        return keylet::marginAccount(account.id(), t.exchange.key);
    }

    [[nodiscard]] static std::string
    str(Number const& value)
    {
        return to_string(value);
    }

    void
    expectError(json::Value const& result, std::string const& error)
    {
        BEAST_EXPECTS(
            result.isMember(jss::error) && result[jss::error].asString() == error,
            to_string(result));
    }

    void
    testExchangeInfo()
    {
        testcase("perp_exchange_info");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        auto t = setup(env);
        trade(env, t);

        // Open interest 4000 at index 2 needs cover 800 of the 1000 deposited.
        json::Value params;
        params[jss::exchange_id] = to_string(t.exchange.key);
        auto result = rpc(env, "perp_exchange_info", params);
        BEAST_EXPECT(result[jss::perp_exchange]["LedgerEntryType"] == "PerpExchange");
        BEAST_EXPECT(result[jss::perp_exchange][jss::index] == to_string(t.exchange.key));
        BEAST_EXPECT(result[jss::perp_exchange][sfCoverAvailable.jsonName] == "1000");
        BEAST_EXPECT(result[jss::perp_exchange][sfOpenInterestNotional.jsonName] == "8000");
        BEAST_EXPECT(result[jss::withdrawable_cover] == "200");
        BEAST_EXPECT(result[jss::haircut] == "0");

        // The same exchange named by its owner and creating sequence.
        auto const sle = env.le(t.exchange);
        if (!BEAST_EXPECT(sle))
            return;
        json::Value byOwner;
        byOwner[jss::owner] = owner_.human();
        byOwner[jss::seq] = (*sle)[sfSequence];
        result = rpc(env, "perp_exchange_info", byOwner);
        BEAST_EXPECT(result[jss::perp_exchange][jss::index] == to_string(t.exchange.key));

        // Index 2.05 raises the open-interest notional to 8200 and the minimum cover to 820.
        movePrice(env, t, 205, 2);
        env(Perp::refresh(carol_, t.market.key));
        env.close();
        result = rpc(env, "perp_exchange_info", params);
        BEAST_EXPECT(result[jss::perp_exchange][sfOpenInterestNotional.jsonName] == "8200");
        BEAST_EXPECT(result[jss::withdrawable_cover] == "180");

        // Bad debt 30 against cash 3000: every margin withdrawal is cut by 1 %, and no cover
        // may leave.
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            Sandbox sb(&view, TapNone);
            auto const exchange = sb.peek(t.exchange);
            if (!BEAST_EXPECT(exchange))
                return false;
            (*exchange)[sfBadDebt] = Number{30};
            (*exchange)[sfCashTotal] = Number{3'000};
            sb.update(exchange);
            sb.apply(view);
            return true;
        });
        params[jss::ledger_index] = "current";
        result = rpc(env, "perp_exchange_info", params);
        BEAST_EXPECT(result[jss::haircut] == "0.01");
        BEAST_EXPECT(result[jss::withdrawable_cover] == "0");
    }

    void
    testMarketInfo()
    {
        testcase("perp_market_info");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        auto t = setup(env);
        trade(env, t);

        json::Value params;
        params[jss::market_id] = to_string(t.market.key);
        auto result = rpc(env, "perp_market_info", params);
        BEAST_EXPECT(result[jss::perp_market]["LedgerEntryType"] == "PerpMarket");
        BEAST_EXPECT(result[jss::perp_market][jss::index] == to_string(t.market.key));
        BEAST_EXPECT(result[jss::perp_market][sfOpenInterestLong.jsonName] == "4000");
        BEAST_EXPECT(result[jss::index_price] == "2");

        // The same market named by its exchange and pair.
        json::Value byPair;
        byPair[jss::exchange_id] = to_string(t.exchange.key);
        byPair[jss::base_asset] = "XRP";
        byPair[jss::quote_asset] = "USD";
        result = rpc(env, "perp_market_info", byPair);
        BEAST_EXPECT(result[jss::perp_market][jss::index] == to_string(t.market.key));

        // The oracles move to 2.05: the stored index stays 2 until a refresh, the live index
        // and rate are what the next refresh in the open ledger writes.
        movePrice(env, t, 205, 2);
        params[jss::ledger_index] = "current";
        result = rpc(env, "perp_market_info", params);
        BEAST_EXPECT(result[jss::perp_market][sfIndexPrice.jsonName] == "2");
        BEAST_EXPECT(result[jss::index_price] == "2.05");
        auto const rate = result[jss::next_funding_rate].asString();

        env(Perp::refresh(carol_, t.market.key));
        auto const market = env.le(t.market);
        if (!BEAST_EXPECT(market))
            return;
        BEAST_EXPECT(str((*market)[sfIndexPrice]) == "2.05");
        BEAST_EXPECTS(str((*market)[sfFundingRate]) == rate, rate);

        // Once refreshed in a ledger, the live values are the stored ones.
        result = rpc(env, "perp_market_info", params);
        BEAST_EXPECT(result[jss::perp_market][sfIndexPrice.jsonName] == "2.05");
        BEAST_EXPECT(result[jss::next_funding_rate] == rate);
        env.close();

        // With every quote older than perp::kIndexMaxAge the market has no live index and
        // accrues no funding.
        env.close(env.now() + perp::kIndexMaxAge + std::chrono::seconds{10});
        result = rpc(env, "perp_market_info", params);
        BEAST_EXPECT(!result.isMember(jss::index_price));
        BEAST_EXPECT(result[jss::next_funding_rate] == "0");
    }

    /**
     * Checks a margin_account_info result against the PerpHelpers functions on view.
     */
    void
    expectHelpers(json::Value const& result, ReadView const& view, Keylet const& marginAccount)
    {
        auto const sle = view.read(marginAccount);
        if (!BEAST_EXPECT(sle))
            return;
        auto const summary = perp::liquidationSummary(view, *sle);
        auto const available = perp::withdrawable(view, *sle);
        if (!BEAST_EXPECT(summary && available))
            return;
        BEAST_EXPECTS(result[jss::equity] == str(summary->equity), to_string(result));
        BEAST_EXPECT(result[jss::im] == str(summary->requirements.initial));
        BEAST_EXPECT(result[jss::mm] == str(summary->requirements.maintenance));
        BEAST_EXPECT(result[jss::withdrawable] == str(std::max(*available, Number{})));
        BEAST_EXPECT(
            result[jss::liquidatable].asBool() ==
            (summary->equity < summary->requirements.maintenance));
    }

    void
    testMarginAccountInfo()
    {
        testcase("margin_account_info");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        auto t = setup(env);
        trade(env, t);

        // At entry: equity 1000, IM 4000 x 2 x 10 % = 800, MM 400, withdrawable 1000 - 800.
        // alice is liquidated where 1000 + 4000 (p - 2) = 4000 p x 5 %, at p = 7000 / 3800.
        json::Value params;
        params[jss::margin_account_id] = to_string(marginKeylet(alice_, t).key);
        params[jss::ledger_index] = env.closed()->seq();
        auto result = rpc(env, "margin_account_info", params);
        BEAST_EXPECT(result[jss::margin_account]["LedgerEntryType"] == "MarginAccount");
        BEAST_EXPECT(result[jss::equity] == "1000");
        BEAST_EXPECT(result[jss::im] == "800");
        BEAST_EXPECT(result[jss::mm] == "400");
        BEAST_EXPECT(result[jss::withdrawable] == "200");
        BEAST_EXPECT(result[jss::liquidatable] == false);
        BEAST_EXPECT(result[jss::waterfall_credit] == "0");
        expectHelpers(result, *env.closed(), marginKeylet(alice_, t));
        if (BEAST_EXPECT(result[jss::positions].isArray() && result[jss::positions].size() == 1))
        {
            auto const& position = result[jss::positions][0u];
            BEAST_EXPECT(position[jss::market_id] == to_string(t.market.key));
            BEAST_EXPECT(position[jss::size] == "4000");
            BEAST_EXPECT(position[jss::entry_price] == "2");
            BEAST_EXPECT(position[jss::upnl] == "0");
            BEAST_EXPECT(position[jss::funding_owed] == "0");
            BEAST_EXPECT(position[jss::loss_owed] == "0");
            BEAST_EXPECT(
                position[jss::estimated_liquidation_price] == str(Number{7'000} / Number{3'800}));
        }

        // bob short 4000 is liquidated where 1000 - 4000 (p - 2) = 4000 p x 5 %.
        json::Value byAccount;
        byAccount[jss::account] = bob_.human();
        byAccount[jss::exchange_id] = to_string(t.exchange.key);
        result = rpc(env, "margin_account_info", byAccount);
        if (BEAST_EXPECT(result[jss::positions].size() == 1))
        {
            auto const& position = result[jss::positions][0u];
            BEAST_EXPECT(position[jss::size] == "-4000");
            BEAST_EXPECT(position[jss::entry_price] == "2");
            BEAST_EXPECT(
                position[jss::estimated_liquidation_price] == str(Number{9'000} / Number{4'200}));
        }

        // At index 2.05 alice gains 200 less funding; the RPC values are what settling the
        // account in the open ledger finds.
        movePrice(env, t, 205, 2);
        params[jss::ledger_index] = "current";
        result = rpc(env, "margin_account_info", params);
        BEAST_EXPECT(result[jss::im] == "820");
        BEAST_EXPECT(result[jss::mm] == "410");
        BEAST_EXPECT(result[jss::liquidatable] == false);
        if (BEAST_EXPECT(result[jss::positions].size() == 1))
        {
            auto const& position = result[jss::positions][0u];
            BEAST_EXPECT(position[jss::upnl] == "200");
            BEAST_EXPECT(position[jss::funding_owed] != "0");
        }
        auto const before = env.le(marginKeylet(alice_, t));
        if (!BEAST_EXPECT(before))
            return;
        env(Perp::refreshAccount(carol_, marginKeylet(alice_, t).key));
        expectHelpers(result, *env.current(), marginKeylet(alice_, t));
        auto const after = env.le(marginKeylet(alice_, t));
        if (!BEAST_EXPECT(after))
            return;
        BEAST_EXPECT(
            str(Number{(*before)[sfCash]} - Number{(*after)[sfCash]}) ==
            result[jss::positions][0u][jss::funding_owed].asString());
        env.close();

        // At index 1.8 alice's equity of about 200 is below MM 360.
        movePrice(env, t, 18, 1);
        result = rpc(env, "margin_account_info", params);
        BEAST_EXPECT(result[jss::mm] == "360");
        BEAST_EXPECT(result[jss::liquidatable] == true);
        BEAST_EXPECT(result[jss::withdrawable] == "0");
        env(Perp::refreshAccount(carol_, marginKeylet(alice_, t).key));
        expectHelpers(result, *env.current(), marginKeylet(alice_, t));

        // carol has no position: no requirement and no positions listed.
        json::Value carol;
        carol[jss::account] = carol_.human();
        carol[jss::exchange_id] = to_string(t.exchange.key);
        result = rpc(env, "margin_account_info", carol);
        BEAST_EXPECT(result[jss::equity] == "1000");
        BEAST_EXPECT(result[jss::im] == "0");
        BEAST_EXPECT(result[jss::withdrawable] == "1000");
        BEAST_EXPECT(result[jss::positions].isArray() && result[jss::positions].size() == 0);
    }

    /**
     * carol's resting book: bids 2.00 x 20, 1.95 x 100, 1.95 x 50, 1.90 x 30 and asks
     * 2.08 x 20, 2.10 x 10, each price level in arrival order.
     */
    void
    restBook(Env& env, Trading const& t) const
    {
        using namespace test::jtx;
        env(Perp::order(carol_, t.market.key, Number{195, -2}, Number{100}));
        env(Perp::order(carol_, t.market.key, Number{195, -2}, Number{50}));
        env(Perp::order(carol_, t.market.key, Number{190, -2}, Number{30}));
        env(Perp::order(carol_, t.market.key, Number{2}, Number{20}));
        env(Perp::order(carol_, t.market.key, Number{210, -2}, Number{10}), Txflags(tfPerpSell));
        env(Perp::order(carol_, t.market.key, Number{208, -2}, Number{20}), Txflags(tfPerpSell));
        env.close();
    }

    void
    expectOrder(json::Value const& order, std::string const& price, std::string const& size)
    {
        BEAST_EXPECTS(
            order[sfLimitPrice.jsonName] == price && order[sfSize.jsonName] == size,
            to_string(order));
    }

    void
    testBookOrders()
    {
        testcase("perp_book_orders");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        auto t = setup(env);
        restBook(env, t);

        json::Value params;
        params[jss::market_id] = to_string(t.market.key);
        params[jss::side] = "bid";
        auto result = rpc(env, "perp_book_orders", params);
        BEAST_EXPECT(result[jss::market_id] == to_string(t.market.key));
        BEAST_EXPECT(result[jss::side] == "bid");
        BEAST_EXPECT(!result.isMember(jss::marker));
        if (BEAST_EXPECT(result[jss::orders].size() == 4))
        {
            expectOrder(result[jss::orders][0u], "2", "20");
            expectOrder(result[jss::orders][1u], "1.95", "100");
            expectOrder(result[jss::orders][2u], "1.95", "50");
            expectOrder(result[jss::orders][3u], "1.9", "30");
            BEAST_EXPECT(result[jss::orders][0u][jss::index].isString());
        }

        params[jss::side] = "ask";
        result = rpc(env, "perp_book_orders", params);
        if (BEAST_EXPECT(result[jss::orders].size() == 2))
        {
            expectOrder(result[jss::orders][0u], "2.08", "20");
            expectOrder(result[jss::orders][1u], "2.1", "10");
        }

        // Two at a time: the marker resumes inside the 1.95 level.
        params[jss::side] = "bid";
        params[jss::limit] = 2;
        result = rpc(env, "perp_book_orders", params);
        if (BEAST_EXPECT(result[jss::orders].size() == 2 && result.isMember(jss::marker)))
        {
            expectOrder(result[jss::orders][1u], "1.95", "100");
            params[jss::marker] = result[jss::marker];
            result = rpc(env, "perp_book_orders", params);
            BEAST_EXPECT(!result.isMember(jss::marker));
            if (BEAST_EXPECT(result[jss::orders].size() == 2))
            {
                expectOrder(result[jss::orders][0u], "1.95", "50");
                expectOrder(result[jss::orders][1u], "1.9", "30");
            }
        }

        // Aggregated, the limit counts price levels.
        params.removeMember(jss::marker);
        params[jss::aggregate] = true;
        result = rpc(env, "perp_book_orders", params);
        BEAST_EXPECT(result[jss::aggregate] == true);
        if (BEAST_EXPECT(result[jss::orders].size() == 2 && result.isMember(jss::marker)))
        {
            BEAST_EXPECT(result[jss::orders][0u][jss::price] == "2");
            BEAST_EXPECT(result[jss::orders][0u][jss::size] == "20");
            BEAST_EXPECT(result[jss::orders][1u][jss::price] == "1.95");
            BEAST_EXPECT(result[jss::orders][1u][jss::size] == "150");
            params[jss::marker] = result[jss::marker];
            result = rpc(env, "perp_book_orders", params);
            BEAST_EXPECT(!result.isMember(jss::marker));
            if (BEAST_EXPECT(result[jss::orders].size() == 1))
            {
                BEAST_EXPECT(result[jss::orders][0u][jss::price] == "1.9");
                BEAST_EXPECT(result[jss::orders][0u][jss::size] == "30");
            }
        }

        // A side with no order.
        Env empty{*this, testableAmendments()};
        auto const bare = setup(empty);
        json::Value none;
        none[jss::market_id] = to_string(bare.market.key);
        none[jss::side] = "ask";
        result = rpc(empty, "perp_book_orders", none);
        BEAST_EXPECT(result[jss::orders].isArray() && result[jss::orders].size() == 0);
    }

    void
    testAccountObjects()
    {
        testcase("account_objects type filters");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        auto t = setup(env);
        restBook(env, t);

        auto const count = [&](Account const& account, std::string const& type) {
            json::Value params;
            params[jss::account] = account.human();
            params[jss::type] = type;
            auto const result = rpc(env, "account_objects", params);
            return result[jss::account_objects].size();
        };
        BEAST_EXPECT(count(owner_, "perp_exchange") == 1);
        BEAST_EXPECT(count(owner_, "perp_market") == 1);
        BEAST_EXPECT(count(carol_, "margin_account") == 1);
        BEAST_EXPECT(count(carol_, "margin_position") == 1);
        BEAST_EXPECT(count(carol_, "perp_order") == 6);
        BEAST_EXPECT(count(alice_, "perp_order") == 0);
    }

    void
    testErrors()
    {
        testcase("errors");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        auto t = setup(env);
        restBook(env, t);

        uint256 const unknown{1};
        struct Method
        {
            std::string name;
            json::StaticString field;
            uint256 key;
        };
        std::vector<Method> const methods{
            {.name = "perp_exchange_info", .field = jss::exchange_id, .key = t.exchange.key},
            {.name = "perp_market_info", .field = jss::market_id, .key = t.market.key},
            {.name = "margin_account_info",
             .field = jss::margin_account_id,
             .key = marginKeylet(carol_, t).key},
            {.name = "perp_book_orders", .field = jss::market_id, .key = t.market.key}};
        for (auto const& method : methods)
        {
            auto const withSide = [&](json::Value params) {
                if (method.name == "perp_book_orders")
                    params[jss::side] = "bid";
                return params;
            };

            expectError(
                rpc(env, method.name, withSide(json::Value{json::ValueType::Object})),
                "invalidParams");

            json::Value params;
            params[method.field] = "zz";
            expectError(rpc(env, method.name, withSide(params)), "invalidParams");
            params[method.field] = 7;
            expectError(rpc(env, method.name, withSide(params)), "invalidParams");

            params[method.field] = to_string(unknown);
            expectError(rpc(env, method.name, withSide(params)), "entryNotFound");

            params[method.field] = to_string(method.key);
            params[jss::ledger_index] = 1'000'000;
            expectError(rpc(env, method.name, withSide(params)), "lgrNotFound");
        }

        // Each id names an entry of its own type only.
        json::Value wrongType;
        wrongType[jss::market_id] = to_string(t.exchange.key);
        expectError(rpc(env, "perp_market_info", wrongType), "entryNotFound");

        json::Value badOwner;
        badOwner[jss::owner] = "not an account";
        badOwner[jss::seq] = 1;
        expectError(rpc(env, "perp_exchange_info", badOwner), "actMalformed");

        json::Value badAccount;
        badAccount[jss::account] = "not an account";
        badAccount[jss::exchange_id] = to_string(t.exchange.key);
        expectError(rpc(env, "margin_account_info", badAccount), "actMalformed");

        json::Value badPair;
        badPair[jss::exchange_id] = to_string(t.exchange.key);
        badPair[jss::base_asset] = "XRP";
        expectError(rpc(env, "perp_market_info", badPair), "invalidParams");

        json::Value book;
        book[jss::market_id] = to_string(t.market.key);
        expectError(rpc(env, "perp_book_orders", book), "invalidParams");
        book[jss::side] = "middle";
        expectError(rpc(env, "perp_book_orders", book), "invalidParams");
        book[jss::side] = "bid";
        book[jss::limit] = 0;
        expectError(rpc(env, "perp_book_orders", book), "invalidParams");
        book.removeMember(jss::limit);
        book[jss::aggregate] = "yes";
        expectError(rpc(env, "perp_book_orders", book), "invalidParams");
        book.removeMember(jss::aggregate);
        book[jss::marker] = "zz";
        expectError(rpc(env, "perp_book_orders", book), "invalidParams");
        book[jss::marker] = to_string(unknown);
        expectError(rpc(env, "perp_book_orders", book), "invalidParams");

        // A marker from the other side's book is refused.
        book[jss::side] = "ask";
        book[jss::limit] = 1;
        auto const asks = rpc(env, "perp_book_orders", book);
        if (BEAST_EXPECT(asks.isMember(jss::marker)))
        {
            book[jss::side] = "bid";
            book[jss::marker] = asks[jss::marker];
            expectError(rpc(env, "perp_book_orders", book), "invalidParams");
        }
    }

public:
    void
    run() override
    {
        testExchangeInfo();
        testMarketInfo();
        testMarginAccountInfo();
        testBookOrders();
        testAccountObjects();
        testErrors();
    }
};

BEAST_DEFINE_TESTSUITE(PerpRPC, perp, xrpl);

}  // namespace xrpl
