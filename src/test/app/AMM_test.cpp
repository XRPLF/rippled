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
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammAlice.expectAmmInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));

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
            // Charging the AMM's LP the transfer fee. Should we?!!!
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
                env, alice, XRP(10000), USD(10001), 50, 70001, ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env);
            // AMM already exists
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(10000), USD(10000), IOUAmount{10000000, 0}));
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
    run() override
    {
        testInvalidInstance();
        testInstanceCreate();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMM, app, ripple, 2);

}  // namespace test
}  // namespace ripple