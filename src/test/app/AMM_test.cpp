//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================
#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <boost/regex.hpp>
#include <test/jtx.h>
#include <test/jtx/AMM.h>

#include <chrono>
#include <utility>

namespace ripple {
namespace test {

template <typename E>
Json::Value
rpc(E& env, std::string const& command, Json::Value const& v)
{
    return env.rpc("json", command, to_string(v));
}

using idmap_t = std::map<std::string, std::string>;

/** Wrapper class. Maintains a map of account id -> name.
 * The map is used to output a user-friendly account name
 * instead of the hash.
 */
class AccountX : public jtx::Account
{
    static inline idmap_t idmap_;

public:
    AccountX(std::string const& name) : jtx::Account(name)
    {
        idmap_[to_string(id())] = name;
    }
    idmap_t const
    idmap() const
    {
        return idmap_;
    }
};

/** Map accound id to name.
 */
std::string
domap(std::string const& s, std::optional<idmap_t> const& idmap)
{
    if (!idmap)
        return s;
    std::string str = s;
    for (auto [id, name] : *idmap)
    {
        boost::regex re(id.c_str());
        str = boost::regex_replace(str, re, name);
    }
    return str;
}

std::vector<std::pair<STAmount, STAmount>>
offersFromJson(Json::Value const& j)
{
    if (!j.isMember("result") || !j["result"].isMember("status") ||
        j["result"]["status"].asString() != "success" ||
        !j["result"].isMember("offers") || !j["result"]["offers"].isArray())
        return {};
    Json::Value offers = j["result"]["offers"];
    std::vector<std::pair<STAmount, STAmount>> res{};
    for (auto it = offers.begin(); it != offers.end(); ++it)
    {
        STAmount gets;
        STAmount pays;
        if (!amountFromJsonNoThrow(gets, (*it)["taker_gets"]) ||
            !amountFromJsonNoThrow(pays, (*it)["taker_pays"]))
            return {};
        res.emplace_back(std::make_pair(gets, pays));
    }
    return res;
}

template <typename E>
Json::Value
readOffers(
    E& env,
    AccountID const& acct,
    std::optional<idmap_t> const& idmap = {})
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    return rpc(env, "account_offers", jv);
}

template <typename E>
Json::Value
readOffers(E& env, AccountX const& acct)
{
    return readOffers(env, acct.id(), acct.idmap());
}

template <typename E>
Json::Value
readLines(
    E& env,
    AccountID const& acctId,
    std::string const& name,
    std::optional<idmap_t> const& idmap = {})
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return rpc(env, "account_lines", jv);
}

template <typename E>
Json::Value
readLines(E& env, AccountX const& acct)
{
    return readLines(env, acct.id(), acct.name(), acct.idmap());
}

class Test : public beast::unit_test::suite
{
protected:
    enum class Fund { All, Acct, None };
    AccountX const gw;
    AccountX const carol;
    AccountX const alice;
    AccountX const bob;
    jtx::IOU const USD;
    jtx::IOU const EUR;
    jtx::IOU const GBP;
    jtx::IOU const BTC;
    jtx::IOU const BAD;

public:
    Test()
        : gw("gateway")
        , carol("carol")
        , alice("alice")
        , bob("bob")
        , USD(gw["USD"])
        , EUR(gw["EUR"])
        , GBP(gw["GBP"])
        , BTC(gw["BTC"])
        , BAD(jtx::IOU(gw, badCurrency()))
    {
    }

protected:
    void
    fund(
        jtx::Env& env,
        jtx::Account const& gw,
        std::vector<jtx::Account> const& accounts,
        std::vector<STAmount> const& amts,
        Fund fund)
    {
        if (fund == Fund::All)
            env.fund(jtx::XRP(30000), gw);
        for (auto const& account : accounts)
        {
            if (fund == Fund::All || fund == Fund::Acct)
                env.fund(jtx::XRP(30000), account);
            for (auto const& amt : amts)
            {
                env.trust(amt + amt, account);
                env(pay(gw, account, amt));
            }
        }
    }

    template <typename F>
    void
    testAMM(
        F&& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = {},
        std::optional<IOUAmount> const& lpt = {},
        std::uint32_t fee = 0,
        std::optional<jtx::ter> const& ter = std::nullopt)
    {
        using namespace jtx;
        Env env{*this};

        auto [asset1, asset2] = [&]() -> std::pair<STAmount, STAmount> {
            if (pool)
                return *pool;
            return {XRP(10000), USD(10000)};
        }();

        fund(
            env,
            gw,
            {alice, carol},
            {STAmount{asset2.issue(), 30000, 0}},
            Fund::All);
        if (!asset1.native())
            fund(
                env,
                gw,
                {alice, carol},
                {STAmount{asset1.issue(), 30000, 0}},
                Fund::None);
        auto tokens = [&]() {
            if (lpt)
                return *lpt;
            return IOUAmount{10000000, 0};
        }();
        AMM ammAlice(env, alice, asset1, asset2, false, fee);
        BEAST_EXPECT(ammAlice.expectBalances(asset1, asset2, tokens));
        cb(ammAlice, env);
    }

    template <typename C>
    void
    stats(C const& t, std::string const& msg)
    {
        auto const sum = std::accumulate(t.begin(), t.end(), 0.0);
        auto const avg = sum / static_cast<double>(t.size());
        auto sd = std::accumulate(
            t.begin(), t.end(), 0.0, [&](auto const init, auto const r) {
                return init + pow((r - avg), 2);
            });
        sd = sqrt(sd / t.size());
        std::cout << msg << " exec time: avg " << avg << " "
                  << " sd " << sd << std::endl;
    }
};

struct AMM_test : public Test
{
public:
    AMM_test() : Test()
    {
    }

private:
    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

        // XRP to IOU
        testAMM([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // IOU to IOU
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                    USD(20000), BTC(0.5), IOUAmount{100, 0}));
            },
            std::make_pair(USD(20000), BTC(0.5)),
            IOUAmount{100, 0});

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, Fund::All);
            env(rate(gw, 1.25));
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            // Charging the AMM's LP the transfer fee.
            env.require(balance(alice, USD(0)));
            env.require(balance(alice, BTC(0)));
        }
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Can't have both XRP tokens
            AMM ammAlice(env, alice, XRP(10000), XRP(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Can't have both tokens the same IOU
            AMM ammAlice(env, alice, USD(10000), USD(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Can't have zero amounts
            AMM ammAlice(env, alice, XRP(0), USD(10000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Bad currency
            AMM ammAlice(
                env, alice, XRP(10000), BAD(10000), ter(temBAD_CURRENCY));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Insufficient IOU balance
            AMM ammAlice(
                env, alice, XRP(10000), USD(40000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Insufficient XRP balance
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            // Invalid trading fee
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10000),
                false,
                70001,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        testAMM([&](AMM& ammAlice, Env& env) {
            AMM ammCarol(
                env, carol, XRP(10000), USD(10000), ter(tecAMM_EXISTS));
        });
    }

    void
    testDeposit()
    {
        testcase("Deposit");

        using namespace jtx;

        // Equal deposit: 1000000 tokens, 10% of the current pool
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal limit deposit: deposit USD100 and XRP proportionally
        // to the pool composition not to exceed 100XRP. If the amount
        // exceeds 100XRP then deposit 100XRP and USD proportionally
        // to the pool composition not to exceed 100USD. Fail if exceeded.
        // Deposit 100USD/100XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Equal limit deposit. Deposit 100USD/100XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(200), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // TODO. Equal limit deposit. Constraint fails.

        // Single deposit: 1000 USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit: 1000 XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(10000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit: 100000 tokens worth of USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10201), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10201), USD(10000), IOUAmount{10100000, 0}));
        });

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(1000), std::nullopt, STAmount{USD, 1, -1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(100), std::nullopt, STAmount{USD, 2004, -6});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, 1008016, -2},
                IOUAmount{10040000, 0}));
        });

        // Single deposit with EP not exceeding specified:
        // 0USD with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(0), std::nullopt, STAmount{USD, 2004, -6});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, 1008016, -2},
                IOUAmount{10040000, 0}));
        });
    }

    void
    testWithdraw()
    {
        testcase("Withdraw");

        using namespace jtx;

        // Fails, Carol is not a Liquidity Provider.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                carol, 10000, std::nullopt, std::optional<ter>(tecAMM_BALANCE));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Fail, Carol withdraws more than she owns.
        testAMM([&](AMM& ammAlice, Env&) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));

            ammAlice.withdraw(
                carol,
                2000000,
                std::nullopt,
                std::optional<ter>(tecAMM_INVALID_TOKENS));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal withdrawal by Carol: 1000000 of tokens, 10% of the current pool
        testAMM([&](AMM& ammAlice, Env&) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1000000, 0}));

            // Carol withdraws all tokens
            ammAlice.withdraw(carol, 1000000);
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
        });

        // Equal withdrawal by tokens 1000000, 10%
        // of the current pool
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(9000), IOUAmount{9000000, 0}));
        });

        // Equal withdrawal with a limit. Withdraw XRP200.
        // If proportional withdraw of USD is less than 100
        // the withdraw that amount, otherwise withdraw USD100
        // and proportionally withdraw XRP. It's the latter
        // in this case - XRP100/USD100.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(200), USD(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Equal withdrawal with a limit. XRP100/USD100.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(100), USD(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Single withdrawal by amount XRP1000
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(10000), IOUAmount{948683298050514, -8}));
        });

        // Single withdrawal by tokens 10000.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 10000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9980.01), IOUAmount{9990000, 0}));
        });

        // Withdraw all tokens. 0 is a special case to withdraw all tokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(alice, 0);
            BEAST_EXPECT(!ammAlice.ammExists());

            // Can create AMM for the XRP/USD pair
            AMM ammCarol(env, carol, XRP(10000), USD(10000));
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, 0, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
        });

        // Single deposit 1000USD, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, 0, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
        });

        // Single deposit/withdrawal 1000USD. Fails due to round-off error,
        // tokens to withdraw exceeds the LP tokens balance.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(10000));
            ammAlice.withdraw(
                carol,
                USD(10000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Single deposit/withdrawal 1000USD
        // There is a round-off error. There remains
        // a dust amount of tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{1000000000000001, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{63, -10}));
        });

        // Single deposit by different accounts and then withdraw
        // in reverse. There is a round-off error. There remains
        // a dust amount of tokens.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.deposit(alice, USD(1000));
            ammAlice.withdraw(alice, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{1000000000000001, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{63, -10}));
        });

        // Equal deposit 10%, withdraw all tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000),
                STAmount{USD, static_cast<std::int64_t>(90909090909091), -10},
                IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
        });

        // Withdraw with EPrice limit.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, USD(100), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11000000000),
                    STAmount{
                        USD, static_cast<std::int64_t>(937278106508876), -11},
                    IOUAmount{1015384615384616, -8}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{153846153846153, -9}));
        });

        // Withdraw with EPrice limit. AssetOut is 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, USD(0), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11000000000),
                    STAmount{
                        USD, static_cast<std::int64_t>(937278106508876), -11},
                    IOUAmount{1015384615384616, -8}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{153846153846153, -9}));
        });

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(
                carol,
                USD(100),
                std::nullopt,
                IOUAmount{500, 0},
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are greater than the LP shares.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(
                carol,
                USD(100),
                std::nullopt,
                IOUAmount{600, 0},
                ter(tecAMM_INVALID_TOKENS));
        });

        // TODO there should be a limit on a single withdrawal amount.
        // For instance, in 10000USD and 10000XRP amm with all liquidity
        // provided by one LP, LP can not withdraw all tokens in USD.
        // Withdrawing 90% in USD is also invalid. Besides the impact
        // on the pool there should be a max threshold for single
        // deposit.
    }

    void
    testRequireAuth()
    {
        testcase("Require Authorization");
        using namespace jtx;

        Env env{*this};
        auto const aliceUSD = alice["USD"];
        env.fund(XRP(20000), alice, gw);
        env(fset(gw, asfRequireAuth));
        env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
        env(trust(alice, USD(10000)));
        env(pay(gw, alice, USD(10000)));
        AMM ammAlice(env, alice, XRP(10000), USD(10000));
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10000), USD(10000), IOUAmount{10000000, 0}, alice));
    }

    void
    testFeeVote()
    {
        testcase("Fee Vote");
        using namespace jtx;

        // Invalid fee.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote({}, 70001, ter(temBAD_FEE));
            auto const jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 0);
        });

        // One vote sets fee to 1%.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote({}, 1000);
            auto const jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 1000);
        });

        // Eight votes fill all voting slots, set fee 2.25%.
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 8; ++i)
            {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, 10000);
                ammAlice.vote(a, 500 * (i + 1));
            }
            auto const jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2250);
        });

        // Eight votes fill all voting slots, set fee 2.25%.
        // New vote, same account, sets fee 2.75%
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](Account const& a, int i) {
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, 10000);
                ammAlice.vote(a, 500 * (i + 1));
            };
            Account a("0");
            vote(a, 0);
            for (int i = 1; i < 8; ++i)
            {
                Account a(std::to_string(i));
                vote(a, i);
            }
            auto jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2250);
            ammAlice.vote(a, 4500);
            jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2750);
        });

        // Eight votes fill all voting slots, set fee 2.25%.
        // New vote, new account, same vote weight, fee is unchanged.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](int i) {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, 10000);
                ammAlice.vote(a, 500 * (i + 1));
            };
            for (int i = 0; i < 8; ++i)
                vote(i);
            auto jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2250);
            vote(8);
            jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2250);
        });

        // Eight votes fill all voting slots, set fee 2.25%.
        // New vote, new account, higher vote weight, set higher fee 2.945%
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](int i, std::uint32_t tokens) {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, tokens);
                ammAlice.vote(a, 500 * (i + 1));
            };
            for (int i = 0; i < 8; ++i)
                vote(i, 10000);
            auto jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2250);
            vote(8, 20000);
            jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2945);
        });

        // Eight votes fill all voting slots, set fee 2.75%.
        // New vote, new account, higher vote weight, set smaller fee 2.056%
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](int i, std::uint32_t tokens) {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, tokens);
                ammAlice.vote(a, 500 * (i + 1));
            };
            for (int i = 8; i > 0; --i)
                vote(i, 10000);
            auto jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2750);
            vote(0, 20000);
            jv = ammAlice.ammRpcInfo();
            BEAST_EXPECT(jv && (*jv)[jss::TradingFee].asUInt() == 2056);
        });
    }

    void
    testBid()
    {
        testcase("Bid");
        using namespace jtx;
        using namespace std::chrono;

        // Bid price is 0, transaction fails.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(carol, 0, std::nullopt, {}, ter(temBAD_AMM_TOKENS));
        });

        // Bid invalid options, transaction fails.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(carol, 100, 100, {}, ter(temBAD_AMM_OPTIONS));
        });

        // Bid price exceeds LP owned tokens, transaction fails.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol, 1000001, std::nullopt, {}, ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                carol, std::nullopt, 1000001, {}, ter(tecAMM_INVALID_TOKENS));
        });

        // Auth account is invalid.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.bid(carol, 100, std::nullopt, {bob}, ter(terNO_ACCOUNT));
        });

        // Bid 100 tokens. The slot is not owned and the MinSlotPrice is 110
        // (currently 0.001% of the pool token balance).
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(carol, 100);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110, 0}));
            // 100 tokens are burned.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{10999890, 0}));
        });

        // Start bid at computed price. The slot is not owned and the
        // MinSlotPrice is 110.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            // Bid, pay the computed price.
            ammAlice.bid(carol);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110, 0}));

            fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
            ammAlice.deposit(bob, 1000000);
            // Bid, pay the computed price.
            ammAlice.bid(bob);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{1155, -1}));

            // Bid MaxSlotPrice fails because the computed price is higher.
            ammAlice.bid(carol, std::nullopt, 120, {}, ter(tecAMM_FAILED_BID));
            // Bid MaxSlotPrice succeeds.
            ammAlice.bid(carol, std::nullopt, 135);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{135, 0}));
        });

        // Slot states.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto constexpr intervalDuration = 24 * 3600 / 20;
            ammAlice.deposit(carol, 1000000);
            fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
            ammAlice.deposit(bob, 1000000);
            env.close();

            // Initial state, not owned. Default MinSlotPrice.
            ammAlice.bid(carol);
            env.close();
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{120, 0}));

            // 1st Interval.
            ammAlice.bid(bob);
            env.close(seconds(intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 1, IOUAmount{126, 0}));

            // 10th Interval.
            ammAlice.bid(carol);
            env.close(seconds(10 * intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 10, IOUAmount{2520609925373213, -13}));

            // 20th Interval - expired.
            ammAlice.bid(bob);
            env.close(seconds(20 * intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 0, IOUAmount{3846625271031949, -13}));

            // 0 Interval.
            ammAlice.bid(carol);
            env.close();
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 0, IOUAmount{1199963692951085, -13}));
            // Tokens burned on bidding fees
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{1199951693314156, -8}));
        });

        // Pool's fee 1%. Bid to pay computed price.
        // Auction slot owner and auth account trade at discounted fee (0).
        // Other accounts trade at 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
                ammAlice.deposit(bob, 1000000);
                ammAlice.deposit(carol, 1000000);
                ammAlice.bid(carol, std::nullopt, std::nullopt, {bob});
                BEAST_EXPECT(
                    ammAlice.expectAuctionSlot(0, 0, IOUAmount{120, 0}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000), USD(12000), IOUAmount{11999880, 0}));
                // Discounted trade
                for (int i = 0; i < 10; ++i)
                {
                    ammAlice.deposit(carol, USD(100));
                    ammAlice.withdraw(carol, USD(100));
                    ammAlice.deposit(bob, USD(100));
                    ammAlice.withdraw(bob, USD(100));
                }
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000), USD(12000), IOUAmount{119998799999998, -7}));
                // Trade with the fee
                for (int i = 0; i < 10; ++i)
                {
                    ammAlice.deposit(alice, USD(100));
                    ammAlice.withdraw(alice, USD(100));
                }
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000), USD(12000), IOUAmount{119948890826098, -7}));
            },
            std::nullopt,
            std::nullopt,
            1000);
    }

    void
    testInvalidAMMPayment()
    {
        testcase("Invalid AMM Payment");
        using namespace jtx;

        // Can't pay into/out of AMM account.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.memoize(ammAlice.ammAccount());
            env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                ter(tecAMM_DIRECT_PAYMENT));
            env(pay(ammAlice.ammAccount(), carol, XRP(10)),
                ter(tefMASTER_DISABLED));
            env(pay(carol, ammAlice.ammAccount(), USD(10)),
                ter(tecAMM_DIRECT_PAYMENT));
            env(pay(ammAlice.ammAccount(), carol, USD(10)),
                ter(tefMASTER_DISABLED));
        });
    }

    void
    testAmendment()
    {
        testcase("Amendment");
    }

    void
    testFees()
    {
        testcase("Fees");
    }

    void
    run() override
    {
        testInvalidInstance();
        testInstanceCreate();
        testDeposit();
        testWithdraw();
        testRequireAuth();
        testFeeVote();
        testBid();
        testInvalidAMMPayment();
    }
};

struct AMM_manual_test : public Test
{
    void
    testFibonnaciPerf()
    {
        testcase("Performance Fibonnaci");
        using namespace std::chrono;
        auto const start = high_resolution_clock::now();

        auto const fee = Number(1) / 100;
        auto const c1_fee = 1 - fee;
        Number poolPays = 1000000;
        Number poolGets = 1000000;
        auto SP = poolPays / (poolGets * c1_fee);
        auto ftakerPays = (Number(5) / 10000) * poolGets / 2;
        auto ftakerGets = SP * ftakerPays;
        poolGets += ftakerPays;
        poolPays -= ftakerGets;
        auto product = poolPays * poolGets;
        Number x(0);
        Number y = ftakerGets;
        Number ftotal(0);
        for (int i = 0; i < 100; ++i)
        {
            ftotal = x + y;
            ftakerGets = ftotal;
            auto ftakerPaysPrime = product / (poolPays - ftakerGets) - poolGets;
            ftakerPays = ftakerPaysPrime / c1_fee;
            poolGets += ftakerPays;
            poolPays -= ftakerGets;
            x = y;
            y = ftotal;
            product = poolPays * poolGets;
        }
        auto const elapsed = high_resolution_clock::now() - start;

        std::cout << "100 fibonnaci "
                  << duration_cast<std::chrono::microseconds>(elapsed).count()
                  << std::endl;
        BEAST_EXPECT(true);
    }

    void
    testOffersPerf()
    {
        testcase("Performance Offers");

        auto const N = 10;
        std::array<std::uint64_t, N> t;

        for (auto i = 0; i < N; i++)
        {
            using namespace jtx;
            Env env(*this);

            env.fund(XRP(1000), alice, carol, bob, gw);
            env.trust(USD(1000), carol);
            env.trust(EUR(1000), alice);
            env.trust(USD(1000), bob);

            env(pay(gw, alice, EUR(1000)));
            env(pay(gw, bob, USD(1000)));

            env(offer(bob, EUR(1000), USD(1000)));

            auto start = std::chrono::high_resolution_clock::now();
            env(pay(alice, carol, USD(1000)), path(~USD), sendmax(EUR(1000)));
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            std::uint64_t microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                    .count();
            t[i] = microseconds;
        }
        stats(t, "single offer");

        for (auto i = 0; i < N; i++)
        {
            using namespace jtx;
            Env env(*this);

            env.fund(XRP(1000), alice, carol, bob, gw);
            env.trust(USD(1000), carol);
            env.trust(EUR(1100), alice);
            env.trust(USD(1000), bob);

            env(pay(gw, alice, EUR(1100)));
            env(pay(gw, bob, USD(1000)));

            for (auto j = 0; j < 10; j++)
                env(offer(bob, EUR(100 + j), USD(100)));

            auto start = std::chrono::high_resolution_clock::now();
            env(pay(alice, carol, USD(1000)), path(~USD), sendmax(EUR(1100)));
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            std::uint64_t microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                    .count();
            t[i] = microseconds;
        }
        stats(t, "multiple offers");
    }

    void
    run() override
    {
        testFibonnaciPerf();
    }
};

BEAST_DEFINE_TESTSUITE(AMM, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMM_manual, tx, ripple);

}  // namespace test
}  // namespace ripple