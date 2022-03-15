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
#include <boost/regex.hpp>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/PathSet.h>

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

template <typename E>
void
readOffers(
    E& env,
    AccountID const& acct,
    std::optional<idmap_t> const& idmap = {})
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    auto const r = rpc(env, "account_offers", jv);
    std::cout << "offers " << domap(r.toStyledString(), idmap) << std::endl;
}

template <typename E>
void
readOffers(E& env, AccountX const& acct)
{
    readOffers(env, acct.id(), acct.idmap());
}

template <typename E>
void
readLines(
    E& env,
    AccountID const& acctId,
    std::string const& name,
    std::optional<idmap_t> const& idmap = {})
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    auto const r = rpc(env, "account_lines", jv);
    std::cout << name << " account lines " << domap(r.toStyledString(), idmap)
              << std::endl;
}

template <typename E>
void
readLines(E& env, AccountX const& acct)
{
    readLines(env, acct.id(), acct.name(), acct.idmap());
}

struct AMM_test : public beast::unit_test::suite
{
    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

        auto const gw = AccountX{"gateway"};
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        AccountX const alice{"alice"};
        AccountX const carol{"carol"};

        auto fund = [&](auto& env) {
            env.fund(XRP(20000), alice, carol, gw);
            env.trust(USD(10000), alice);
            env.trust(USD(25000), carol);
            env.trust(BTC(0.625), carol);

            env(pay(gw, alice, USD(10000)));
            env(pay(gw, carol, USD(25000)));
            env(pay(gw, carol, BTC(0.625)));
        };

        {
            Env env{*this};
            fund(env);
            // XRP to IOU
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}, alice));
            BEAST_EXPECT(ammAlice.expectAmmInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}, alice));

            // IOU to IOU
            AMM ammCarol(env, carol, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammCarol.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(ammCarol.expectAmmInfo(
                USD(20000), BTC(0.5), IOUAmount{100, 0}, carol));
        }

        {
            Env env{*this};
            fund(env);
            env(rate(gw, 1.25));
            // IOU to IOU
            AMM ammCarol(env, carol, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammCarol.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            // Charging the AMM's LP the transfer fee.
            env.require(balance(carol, USD(0)));
            env.require(balance(carol, BTC(0)));
        }
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const BAD = IOU(gw, badCurrency());
        Account const alice{"alice"};
        Account const carol{"carol"};

        auto fund = [&](auto& env) {
            env.fund(XRP(30000), alice, carol, gw);
            env.trust(USD(30000), alice);
            env.trust(USD(30000), carol);

            env(pay(gw, alice, USD(30000)));
            env(pay(gw, carol, USD(30000)));
        };

        {
            Env env{*this};
            fund(env);
            // Can't have both XRP
            AMM ammAlice(env, alice, XRP(10000), XRP(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Can't have both IOU
            AMM ammAlice(env, alice, USD(10000), USD(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Can't have zero amounts
            AMM ammAlice(env, alice, XRP(0), USD(0), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Bad currency
            AMM ammAlice(
                env, alice, XRP(10000), BAD(10000), ter(temBAD_CURRENCY));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Insufficient IOU balance
            AMM ammAlice(
                env, alice, XRP(10000), USD(40000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Insufficient XRP balance
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Invalid trading fee
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10001),
                false,
                50,
                70001,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // AMM already exists
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            AMM ammCarol(env, carol, XRP(10000), USD(10000), ter(tefINTERNAL));
        }

        {
            Env env{*this};
            fund(env);
            // AMM already exists
            auto const ammAccount = calcAMMAccountID(50, XRP, USD);
            env(amm::pay(gw, ammAccount, XRP(10000)));
            AMM ammCarol(env, carol, XRP(10000), USD(10000), ter(tefINTERNAL));
        }
    }

    void
    testAddLiquidity()
    {
        testcase("Add Liquidity");

        using namespace jtx;

        auto const gw = AccountX{"gateway"};
        auto const USD = gw["USD"];
        AccountX const alice{"alice"};
        AccountX const carol{"carol"};

        auto proc = [&](auto cb,
                        std::optional<std::pair<std::uint32_t, std::uint32_t>>
                            pool = {},
                        std::optional<std::uint32_t> lpt = {}) {
            Env env{*this};
            env.fund(XRP(30000), alice, carol, gw);
            env.trust(USD(30000), alice);
            env.trust(USD(30000), carol);

            env(pay(gw, alice, USD(30000)));
            env(pay(gw, carol, USD(30000)));

            auto [asset1, asset2] =
                [&]() -> std::pair<std::uint32_t, std::uint32_t> {
                if (pool)
                    return *pool;
                return {10000, 10000};
            }();
            auto tokens = [&]() -> std::uint32_t {
                if (lpt)
                    return *lpt;
                return 10000000;
            }();
            AMM ammAlice(env, alice, XRP(asset1), USD(asset2));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(asset1), USD(asset2), IOUAmount{tokens, 0}));
            cb(ammAlice);
        };

        // Equal deposit: 1000000 tokens, 10% of the current pool
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal limit deposit: deposit USD100 and XRP proportionally
        // to the pool composition not to exceed 100XRP. If the amount
        // exceeds 100XRP then deposit 100XRP and USD proportionally
        // to the pool composition not to exceed 100USD. Fail if exceeded.
        // Deposit 100USD/100XRP
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, USD(100), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Equal limit deposit. Deposit 100USD/100XRP
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, USD(200), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Single deposit: 1000 USD
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{104880884817015, -7}));
        });

        // Single deposit: 1000 XRP
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(10000), IOUAmount{104880884817015, -7}));
        });

        // Single deposit: 100000 tokens worth of USD
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, 100000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10201), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, 100000, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10201), USD(10000), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(carol, 100000, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10201), USD(10000), IOUAmount{10100000, 0}));
        });

        // Single deposit with SP not exceeding specified:
        // 100USD with SP not to exceed 100000 (USD relative to XRP)
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(
                carol, USD(1000), std::nullopt, XRPAmount{1000000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{104880884817015, -7}));
        });

        // TODO add exceed SP test
    }

    void
    testWithdrawLiquidity()
    {
        testcase("Withdraw Liquidity");

        using namespace jtx;

        auto const gw = AccountX{"gateway"};
        auto const USD = gw["USD"];
        AccountX const alice{"alice"};
        AccountX const carol{"carol"};

        auto proc = [&](auto cb,
                        std::optional<std::pair<std::uint32_t, std::uint32_t>>
                            pool = {},
                        std::optional<IOUAmount> lpt = {}) {
            Env env{*this};
            env.fund(XRP(30000), alice, carol, gw);
            env.trust(USD(30000), alice);
            env.trust(USD(30000), carol);

            env(pay(gw, alice, USD(30000)));
            env(pay(gw, carol, USD(30000)));

            auto [asset1, asset2] =
                [&]() -> std::pair<std::uint32_t, std::uint32_t> {
                if (pool)
                    return *pool;
                return {10000, 10000};
            }();
            auto tokens = [&]() {
                if (lpt)
                    return *lpt;
                return IOUAmount{10000000, 0};
            }();
            AMM ammAlice(env, alice, XRP(asset1), USD(asset2));
            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(asset1), USD(asset2), tokens));
            cb(ammAlice);
        };

        // Should fail - Carol is not a Liquidity Provider.
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(
                carol, 10000, std::nullopt, std::optional<ter>(tecAMM_BALANCE));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Should fail - Carol withdraws more than deposited
        proc([&](AMM& ammAlice) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));

            ammAlice.withdraw(
                carol,
                2000000,
                std::nullopt,
                std::optional<ter>(tecAMM_BALANCE));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal withdraw by Carol: 1000000 of tokens, 10% of the current pool
        proc([&](AMM& ammAlice) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(1000), USD(1000), IOUAmount{1000000, 0}, carol));

            // Carol withdraws all tokens
            ammAlice.withdraw(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(0), USD(0), IOUAmount{0, 0}, carol));
        });

        // Equal withdraw by tokens 1000000, which is 10%
        // of the current pool
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(alice, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(9000), IOUAmount{9000000, 0}));
        });

        // Equal withdraw with a limit. Withdraw XRP200.
        // If proportional withdraw of USD is less than 100
        // the withdraw that amount, otherwise withdraw USD100
        // and proportionally withdraw XRP. It's the latter in
        // in this case - XRP100/USD100.
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(alice, XRP(200), USD(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Equal withdraw with a limit. XRP100/USD100.
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(alice, XRP(100), USD(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Single withdraw by amount XRP100
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(alice, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(10000), IOUAmount{948683298050513, -8}));
        });

        // Single withdraw by tokens 1000.
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(alice, 10000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9980.01), IOUAmount{9990000, 0}));
        });

        // Single withdraw maxSP limit. SP after the trade is 1111111.111,
        // less than 1200000.
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(
                alice, USD(1000), std::nullopt, XRPAmount{1200000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9000), IOUAmount{948683298050513, -8}));
        });

        // Single withdraw maxSP limit. SP after the trade is 1111111.111,
        // greater than 1100000, the withdrawl amount is changed to ~USD488.088
        proc([&](AMM& ammAlice) {
            ammAlice.withdraw(
                alice, USD(1000), std::nullopt, XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD.issue(), 95119115182985llu, -10},
                IOUAmount{9752902910568, -6}));
        });
    }

    void
    testPerformance()
    {
        testcase("Performance");

        auto const N = 1;
        std::vector<std::uint64_t> t(N);
        auto stats = [&](std::string const& msg) {
            auto const avg =
                static_cast<float>(std::accumulate(t.begin(), t.end(), 0)) /
                static_cast<float>(N);
            auto const sd = std::accumulate(
                t.begin(), t.end(), 0., [&](auto accum, auto const& v) {
                    return accum + (v - avg) * (v - avg);
                });
            std::cout << msg << " avg " << avg << " sd "
                      << std::sqrt(sd / static_cast<float>(N)) << std::endl;
        };

        for (auto i = 0; i < N; i++)
        {
            using namespace jtx;
            Env env(*this);

            auto const gw = AccountX("gateway");
            auto const USD = gw["USD"];
            auto const EUR = gw["EUR"];
            AccountX const alice{"alice"};
            AccountX const carol{"carol"};
            AccountX const bob{"bob"};

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
            t.push_back(microseconds);
        }
        stats("single offer");

        t.clear();
        for (auto i = 0; i < N; i++)
        {
            using namespace jtx;
            Env env(*this);

            auto const gw = AccountX("gateway");
            auto const USD = gw["USD"];
            auto const EUR = gw["EUR"];
            AccountX const alice{"alice"};
            AccountX const carol{"carol"};
            AccountX const bob{"bob"};

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
            t.push_back(microseconds);
        }
        stats("multiple offers");
    }

    void
    testSwap()
    {
        testcase("Swap");

        using namespace jtx;

        auto const gw = AccountX{"gateway"};
        auto const USD = gw["USD"];
        AccountX const alice{"alice"};
        AccountX const carol{"carol"};

        auto proc = [&](auto cb,
                        std::optional<std::pair<std::uint32_t, std::uint32_t>>
                            pool = {},
                        std::optional<IOUAmount> lpt = {}) {
            Env env{*this};
            env.fund(XRP(30000), alice, carol, gw);
            env.trust(USD(30000), alice);
            env.trust(USD(30000), carol);

            env(pay(gw, alice, USD(30000)));
            env(pay(gw, carol, USD(30000)));

            auto [asset1, asset2] =
                [&]() -> std::pair<std::uint32_t, std::uint32_t> {
                if (pool)
                    return *pool;
                return {10000, 10000};
            }();
            auto tokens = [&]() {
                if (lpt)
                    return *lpt;
                return IOUAmount{10000000, 0};
            }();
            AMM ammAlice(env, alice, XRP(asset1), USD(asset2));
            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(asset1), USD(asset2), tokens));
            cb(ammAlice);
        };

        // Swap in USD1000
        proc([&](AMM& ammAlice) {
            ammAlice.swapIn(alice, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9090909091}, USD(11000), IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, Slippage not to exceed 10000
        proc([&](AMM& ammAlice) {
            ammAlice.swapIn(alice, USD(1000), 10000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9090909091}, USD(11000), IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, MaxSP not to exceed 1100000
        proc([&](AMM& ammAlice) {
            ammAlice.swapIn(alice, USD(1000), std::nullopt, XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9534625893},
                STAmount{USD.issue(), 104880884817015llu, -10},
                IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, MaxSP not to exceed 110000.
        // This transaction fails.
        proc([&](AMM& ammAlice) {
            ammAlice.swapIn(
                alice,
                USD(1000),
                std::nullopt,
                XRPAmount{110000},
                std::optional<ter>(tecAMM_FAILED_SWAP));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, MaxSP not to exceed 1100000, and Slippage 10000.
        // SP is less than MaxSP - execute swapOut with MaxSP
        // 110000, the trade executes.
        proc([&](AMM& ammAlice) {
            ammAlice.swap(alice, USD(1000), 10000, XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10513133959},
                STAmount{USD.issue(), 95119115182985llu, -10},
                IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, MaxSP not to exceed 100000, and Slippage 10000.
        // SP is less than MaxSP. The SP can't be changed and the trade fails.
        proc([&](AMM& ammAlice) {
            ammAlice.swap(
                alice,
                USD(1000),
                10000,
                XRPAmount{100000},
                std::optional<ter>(tecAMM_FAILED_SWAP));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, MaxSP not to exceed 900000, and Slippage 10000.
        // SP is greater than MaxSP. TODO: But change SP then fails.
        proc([&](AMM& ammAlice) {
            ammAlice.swap(
                alice,
                USD(1000),
                10000,
                XRPAmount{900000},
                std::optional<ter>(tecAMM_FAILED_SWAP));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Swap out
        proc([&](AMM& ammAlice) {
            ammAlice.swapOut(alice, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{11111111111}, USD(9000), IOUAmount{10000000, 0}));
        });

        // Swap out USD1000, MaxSP not to exceed 1100000
        proc([&](AMM& ammAlice) {
            ammAlice.swapOut(alice, USD(1000), XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10513133959},
                STAmount{USD.issue(), 95119115182985llu, -10},
                IOUAmount{10000000, 0}));
        });

        // Swap out USD1000, MaxSP not to exceed 900000
        // This transaction fails.
        proc([&](AMM& ammAlice) {
            ammAlice.swapOut(
                alice,
                USD(1000),
                XRPAmount{900000},
                std::optional<ter>(tecAMM_FAILED_SWAP));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
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
        testAddLiquidity();
        testWithdrawLiquidity();
        testSwap();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMM, app, ripple, 2);

}  // namespace test
}  // namespace ripple