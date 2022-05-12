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
    AccountX const gw;
    AccountX const carol;
    AccountX const alice;
    AccountX const bob;
    jtx::IOU const USD;
    jtx::IOU const EUR;
    jtx::IOU const BTC;
    jtx::IOU const BAD;

public:
    AMM_test()
        : gw("gateway")
        , carol("carol")
        , alice("alice")
        , bob("bob")
        , USD(gw["USD"])
        , EUR(gw["EUR"])
        , BTC(gw["BTC"])
        , BAD(jtx::IOU(gw, badCurrency()))
    {
    }

private:
    template <typename F>
    void
    proc(
        F&& cb,
        std::optional<std::pair<std::uint32_t, std::uint32_t>> const& pool = {},
        std::optional<IOUAmount> const& lpt = {},
        std::uint32_t fee = 0)
    {
        using namespace jtx;
        Env env{*this};

        env.fund(jtx::XRP(30000), alice, carol, gw);
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
        AMM ammAlice(env, alice, XRP(asset1), USD(asset2), false, 50, fee);
        BEAST_EXPECT(ammAlice.expectBalances(XRP(asset1), USD(asset2), tokens));
        cb(ammAlice, env);
    }

    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

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
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}, alice));

            // IOU to IOU
            AMM ammCarol(env, carol, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammCarol.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(ammCarol.expectAmmRpcInfo(
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
            // Can't have both XRP tokens
            AMM ammAlice(env, alice, XRP(10000), XRP(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Can't have both tokens the same IOU
            AMM ammAlice(env, alice, USD(10000), USD(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // Can't have zero amounts
            AMM ammAlice(env, alice, XRP(0), USD(10000), ter(temBAD_AMOUNT));
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
    }

    void
    testAddLiquidity()
    {
        testcase("Add Liquidity");

        using namespace jtx;

        // Equal deposit: 1000000 tokens, 10% of the current pool
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal limit deposit: deposit USD100 and XRP proportionally
        // to the pool composition not to exceed 100XRP. If the amount
        // exceeds 100XRP then deposit 100XRP and USD proportionally
        // to the pool composition not to exceed 100USD. Fail if exceeded.
        // Deposit 100USD/100XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Equal limit deposit. Deposit 100USD/100XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(200), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // TODO. Equal limit deposit. Constraint fails.

        // Single deposit: 1000 USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit: 1000 XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(10000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit: 100000 tokens worth of USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10201), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10201), USD(10000), IOUAmount{10100000, 0}));
        });

#if 0  // specs in works
       // Single deposit with SP not exceeding specified:
       // 100USD with SP not to exceed 100000 (USD relative to XRP)
        proc([&](AMM& ammAlice) {
            ammAlice.deposit(
                carol, USD(1000), std::nullopt, XRPAmount{1000000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{104880884817015, -7}));
        });
#endif
    }

    void
    testWithdrawLiquidity()
    {
        testcase("Withdraw Liquidity");

        using namespace jtx;

        // Should fail - Carol is not a Liquidity Provider.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                carol, 10000, std::nullopt, std::optional<ter>(tecAMM_BALANCE));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Should fail - Carol withdraws more than deposited
        proc([&](AMM& ammAlice, Env&) {
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
        proc([&](AMM& ammAlice, Env&) {
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

        // Equal withdraw by tokens 1000000, 10%
        // of the current pool
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(9000), IOUAmount{9000000, 0}));
        });

        // Equal withdraw with a limit. Withdraw XRP200.
        // If proportional withdraw of USD is less than 100
        // the withdraw that amount, otherwise withdraw USD100
        // and proportionally withdraw XRP. It's the latter
        // in this case - XRP100/USD100.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(200), USD(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Equal withdraw with a limit. XRP100/USD100.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(100), USD(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Single withdraw by amount XRP1000
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(10000), IOUAmount{948683298050514, -8}));
        });

        // Single withdraw by tokens 10000.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 10000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9980.01), IOUAmount{9990000, 0}));
        });

#if 0  // specs in works
       // Single withdraw maxSP limit. SP after the trade is 1111111.111,
       // less than 1200000.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice, USD(1000), std::nullopt, XRPAmount{1200000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9000), IOUAmount{948683298050513, -8}));
        });

        // Single withdraw maxSP limit. SP after the trade is 1111111.111,
        // greater than 1100000, the withdrawl amount is changed to ~USD488.088
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice, USD(1000), std::nullopt, XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD.issue(), 95119115182985llu, -10},
                IOUAmount{9752902910568, -6}));
        });

#endif

        // Withdraw all tokens. 0 is a special case to withdraw all tokens.
        proc([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(alice, 0);
            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(0), USD(0), IOUAmount{0, 0}));

            // Can create AMM for the XRP/USD pair
            AMM ammCarol(env, carol, XRP(10000), USD(10000));
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, 0, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, 0, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
        });

        // Single deposit/withdraw 1000USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000),
                STAmount{USD.issue(), 90909090909091llu, -10},
                IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
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

        // Swap in USD1000
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapIn(alice, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9090909091}, USD(11000), IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, Slippage not to exceed 10000
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapIn(alice, USD(1000), 10000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9090909091}, USD(11000), IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, limitSP not to exceed 1100000
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapIn(alice, USD(1000), std::nullopt, XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9534625893},
                STAmount{USD.issue(), 1048808848170152llu, -11},
                IOUAmount{10000000, 0}));
        });

        // Swap in USD1000, limitSP not to exceed 110000.
        // This transaction fails.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapIn(
                alice,
                USD(1000),
                std::nullopt,
                XRPAmount{110000},
                ter(tecAMM_FAILED_SWAP));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Swap out
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapOut(alice, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{11111111111}, USD(9000), IOUAmount{10000000, 0}));
        });

#if 0
        // Swap out USD1000, limitSP not to exceed 1100000
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapOut(alice, USD(1000), XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10513133959},
                STAmount{USD.issue(), 951191151829848llu, -11},
                IOUAmount{10000000, 0}));
        });

        // Swap out USD1000, limitSP not to exceed 900000
        // This transaction fails.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.swapOut(
                alice,
                USD(1000),
                XRPAmount{900000},
                std::optional<ter>(tecAMM_FAILED_SWAP));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });
#endif
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