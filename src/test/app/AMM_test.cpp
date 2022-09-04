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
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>

#include <chrono>
#include <utility>

namespace ripple {
namespace test {

#if 0
static Json::Value
readOffers(jtx::Env& env, AccountID const& acct)
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    return env.rpc("json", "account_offers", to_string(jv));
}

static Json::Value
readLines(jtx::Env& env, AccountID const& acctId)
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return env.rpc("json", "account_lines", to_string(jv));
}

static Json::Value
accountInfo(jtx::Env& env, AccountID const& acctId)
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return env.rpc("json", "account_info", to_string(jv));
}
#endif

static XRPAmount
txfee(jtx::Env const& env, std::uint16_t n)
{
    return env.current()->fees().base * n;
}

static bool
expectLine(jtx::Env& env, AccountID const& account, STAmount const& value)
{
    if (auto const sle = env.le(keylet::line(account, value.issue())))
    {
        auto amount = sle->getFieldAmount(sfBalance);
        amount.setIssuer(value.issue().account);
        if (account > value.issue().account)
            amount.negate();
        return amount == value;
    }
    return false;
}

static bool
expectOffers(
    jtx::Env& env,
    AccountID const& account,
    std::uint16_t size,
    std::optional<std::vector<Amounts>> const& toMatch = std::nullopt)
{
    std::uint16_t cnt = 0;
    std::uint16_t matched = 0;
    forEachItem(
        *env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
            if (!sle)
                return false;
            if (sle->getType() == ltOFFER)
            {
                ++cnt;
                if (toMatch &&
                    std::find_if(
                        toMatch->begin(), toMatch->end(), [&](auto const& a) {
                            return a.in == sle->getFieldAmount(sfTakerPays) &&
                                a.out == sle->getFieldAmount(sfTakerGets);
                        }) != toMatch->end())
                    ++matched;
            }
            return true;
        });
    return size == cnt && (!toMatch || matched == toMatch->size());
}

static auto
ledgerEntryRoot(jtx::Env& env, jtx::Account const& acct)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::account_root] = acct.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

static bool
expectLedgerEntryRoot(
    jtx::Env& env,
    jtx::Account const& acct,
    STAmount const& expectedValue)
{
    auto const jrr = ledgerEntryRoot(env, acct);
    return jrr[jss::node][sfBalance.fieldName] ==
        to_string(expectedValue.xrp());
}

class Test : public beast::unit_test::suite
{
protected:
    enum class Fund { All, Acct, None };
    jtx::Account const gw;
    jtx::Account const carol;
    jtx::Account const alice;
    jtx::Account const bob;
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
        Fund how)
    {
        fund(env, gw, accounts, 30000 * jtx::dropsPerXRP, amts, how);
    }
    void
    fund(
        jtx::Env& env,
        jtx::Account const& gw,
        std::vector<jtx::Account> const& accounts,
        STAmount const& xrp,
        std::vector<STAmount> const& amts = {},
        Fund how = Fund::All)
    {
        if (how == Fund::All)
            env.fund(xrp, gw);
        env.close();
        for (auto const& account : accounts)
        {
            if (how == Fund::All || how == Fund::Acct)
            {
                env.fund(xrp, account);
                env.close();
            }
            for (auto const& amt : amts)
            {
                env.trust(amt + amt, account);
                env.close();
                env(pay(gw, account, amt));
                env.close();
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
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::optional<FeatureBitset> const& features = std::nullopt)
    {
        using namespace jtx;
        auto env = features ? Env{*this, *features} : Env{*this};

        auto [asset1, asset2] = [&]() -> std::pair<STAmount, STAmount> {
            if (pool)
                return *pool;
            return {XRP(10000), USD(10000)};
        }();

        fund(
            env,
            gw,
            {alice, carol},
            {STAmount{asset2.issue(), 30000}},
            Fund::All);
        if (!asset1.native())
            fund(
                env,
                gw,
                {alice, carol},
                {STAmount{asset1.issue(), 30000}},
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

    XRPAmount
    reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve(count);
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
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // IOU to IOU
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectBalances(
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
            // Transfer fee is not charged.
            BEAST_EXPECT(expectLine(env, alice, USD(5000)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0.125)));
        }

        // Require authorization is set, account is authorized
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(USD(30000), alice);
            env.close();
            env(trust(gw, alice["USD"](30000)), txflags(tfSetfAuth));
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
        }

        // Cleared global freeze
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env.trust(USD(30000), alice);
            env.close();
            AMM ammAliceFail(
                env, alice, XRP(10000), USD(10000), ter(tecFROZEN));
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
        }
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        // Can't have both XRP tokens
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10000), XRP(10000), ter(temBAD_AMM_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have both tokens the same IOU
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, USD(10000), USD(10000), ter(temBAD_AMM_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have zero amounts
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(env, alice, XRP(0), USD(10000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Bad currency
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10000), BAD(10000), ter(temBAD_CURRENCY));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient IOU balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10000), USD(40000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient XRP balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid trading fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10000),
                false,
                65001,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // AMM already exists
        testAMM([&](AMM& ammAlice, Env& env) {
            AMM ammCarol(
                env, carol, XRP(10000), USD(10000), ter(tecAMM_EXISTS));
        });

        // Invalid flags
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10000),
                false,
                0,
                tfAMMWithdrawAll,
                std::nullopt,
                ter(temINVALID_FLAG));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid Account
        {
            Env env{*this};
            Account bad("bad");
            env.memoize(bad);
            AMM ammAlice(
                env,
                bad,
                XRP(10000),
                USD(10000),
                false,
                0,
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Require authorization is set
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, alice["USD"](30000)));
            env.close();
            AMM ammAlice(
                env, alice, XRP(10000), USD(10000), ter(tecNO_PERMISSION));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Global freeze
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(trust(gw, alice["USD"](30000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());
        }
    }

    void
    testInvalidDeposit()
    {
        testcase("Invalid Deposit");

        using namespace jtx;

        // Invalid flags
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                1000000,
                std::nullopt,
                tfAMMWithdrawAll,
                ter(temINVALID_FLAG));
        });

        // Invalid options
        std::vector<std::tuple<
            std::optional<std::uint32_t>,
            std::optional<STAmount>,
            std::optional<STAmount>,
            std::optional<STAmount>>>
            invalidOptions = {
                // tokens, asset1In, asset2in, EPrice
                {1000, std::nullopt, USD(100), std::nullopt},
                {1000, std::nullopt, std::nullopt, STAmount{USD, 1, -1}},
                {std::nullopt, std::nullopt, USD(100), STAmount{USD, 1, -1}},
                {std::nullopt, XRP(100), USD(100), STAmount{USD, 1, -1}},
                {1000, XRP(100), USD(100), std::nullopt}};
        for (auto const& it : invalidOptions)
        {
            testAMM([&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    alice,
                    std::get<0>(it),
                    std::get<1>(it),
                    std::get<2>(it),
                    std::get<3>(it),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_OPTIONS));
            });
        }

        // Invalid tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice, 0, std::nullopt, std::nullopt, ter(temBAD_AMM_TOKENS));
        });

        // Invalid amount value
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
        });

        // Bad currency
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                BAD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));
        });

        // Invalid Account
        testAMM([&](AMM& ammAlice, Env& env) {
            Account bad("bad");
            env.memoize(bad);
            ammAlice.deposit(
                bad,
                1000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.deposit(
                alice, 10000, std::nullopt, std::nullopt, ter(terNO_ACCOUNT));
        });

        // Frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
        });

        // Frozen asset, balance is not available
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            ammAlice.deposit(
                carol,
                1000000,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
        });

        // Insufficient XRP balance
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.close();
            ammAlice.deposit(
                bob,
                XRP(1001),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient USD balance
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
            env.close();
            ammAlice.deposit(
                bob,
                USD(1001),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient USD balance by tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
            env.close();
            ammAlice.deposit(
                bob,
                10000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient XRP balance by tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, bob, USD(90000)));
            env.close();
            ammAlice.deposit(
                bob,
                10000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
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

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, Fund::All);
            env(rate(gw, 1.25));
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            // Transfer fee is not charged.
            BEAST_EXPECT(expectLine(env, alice, USD(5000)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0.125)));
            // LP deposits, doesn't pay transfer fee.
            fund(env, gw, {carol}, {USD(2500), BTC(0.0625)}, Fund::Acct);
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22000), BTC(0.55), IOUAmount{110, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(500)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0.0125)));
        }
    }

    void
    testInvalidWithdraw()
    {
        testcase("Invalid Withdraw");

        using namespace jtx;

        // Invalid flags
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                1000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                tfPartialPayment,
                std::nullopt,
                ter(temINVALID_FLAG));
        });

        // Invalid options
        std::vector<std::tuple<
            std::optional<std::uint32_t>,
            std::optional<STAmount>,
            std::optional<STAmount>,
            std::optional<IOUAmount>,
            std::optional<std::uint32_t>>>
            invalidOptions = {
                // tokens, asset1Out, asset2Out, EPrice, tfAMMWithdrawAll
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt},
                {1000,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 tfAMMWithdrawAll},
                {std::nullopt,
                 std::nullopt,
                 USD(100),
                 std::nullopt,
                 tfAMMWithdrawAll},
                {1000, std::nullopt, USD(100), std::nullopt, std::nullopt},
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 IOUAmount{250, 0},
                 tfAMMWithdrawAll},
                {1000,
                 std::nullopt,
                 std::nullopt,
                 IOUAmount{250, 0},
                 std::nullopt},
                {std::nullopt,
                 std::nullopt,
                 USD(100),
                 IOUAmount{250, 0},
                 std::nullopt},
                {std::nullopt,
                 XRP(100),
                 USD(100),
                 IOUAmount{250, 0},
                 std::nullopt},
                {1000, XRP(100), USD(100), std::nullopt, std::nullopt},
                {std::nullopt,
                 XRP(100),
                 USD(100),
                 std::nullopt,
                 tfAMMWithdrawAll}};
        for (auto const& it : invalidOptions)
        {
            testAMM([&](AMM& ammAlice, Env& env) {
                ammAlice.withdraw(
                    alice,
                    std::get<0>(it),
                    std::get<1>(it),
                    std::get<2>(it),
                    std::get<3>(it),
                    std::get<4>(it),
                    std::nullopt,
                    ter(temBAD_AMM_OPTIONS));
            });
        }

        // Invalid tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice, 0, std::nullopt, std::nullopt, ter(temBAD_AMM_TOKENS));
        });

        // Invalid amount value
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice, USD(0), std::nullopt, std::nullopt, ter(temBAD_AMOUNT));
        });

        // Invalid amount/token value, withdraw all tokens from one side
        // of the pool.
        {
            testAMM([&](AMM& ammAlice, Env& env) {
                ammAlice.withdraw(
                    alice,
                    USD(10000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
            });

            testAMM([&](AMM& ammAlice, Env& env) {
                ammAlice.withdraw(
                    alice,
                    XRP(10000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
            });

            testAMM([&](AMM& ammAlice, Env& env) {
                ammAlice.withdraw(
                    alice,
                    std::nullopt,
                    USD(0),
                    std::nullopt,
                    std::nullopt,
                    tfAMMWithdrawAll,
                    std::nullopt,
                    ter(tecAMM_BALANCE));
            });
        }

        // Bad currency
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                BAD(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));
        });

        // Invalid Account
        testAMM([&](AMM& ammAlice, Env& env) {
            Account bad("bad");
            env.memoize(bad);
            ammAlice.withdraw(
                bad,
                1000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.withdraw(
                alice, 10000, std::nullopt, std::nullopt, ter(terNO_ACCOUNT));
        });

        // Frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            ammAlice.withdraw(
                carol, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Frozen asset, balance is not available
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            ammAlice.withdraw(
                carol, 1000, std::nullopt, std::nullopt, ter(tecAMM_BALANCE));
        });

        // Carol is not a Liquidity Provider
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                carol, 10000, std::nullopt, std::nullopt, ter(tecAMM_BALANCE));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Carol withdraws more than she owns
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
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
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

        // Withdraw with EPrice limit. Fails to withdraw, amount1
        // to withdraw is less than 1700USD.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(
                carol,
                USD(1700),
                std::nullopt,
                IOUAmount{520, 0},
                ter(tecAMM_FAILED_WITHDRAW));
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
    }

    void
    testWithdraw()
    {
        testcase("Withdraw");

        using namespace jtx;

        // Equal withdrawal by Carol: 1000000 of tokens, 10% of the current
        // pool
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

        // Withdraw all tokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());

            // Can create AMM for the XRP/USD pair
            AMM ammCarol(env, carol, XRP(10000), USD(10000));
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdrawAll(carol, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(999999999999999), -11},
                IOUAmount{10000000, 0}));
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
        });

        // Single deposit 1000USD, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdrawAll(carol, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
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
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdrawAll(carol, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000),
                STAmount{USD, UINT64_C(9090909090909092), -12},
                IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdrawAll(carol, XRP(0));
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
                    STAmount{USD, UINT64_C(9372781065088756), -12},
                    IOUAmount{1015384615384615, -8}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{153846153846153, -9}));
        });

        // Withdraw with EPrice limit. AssetOut is 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, USD(0), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11000000000),
                    STAmount{USD, UINT64_C(9372781065088756), -12},
                    IOUAmount{1015384615384615, -8}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{153846153846153, -9}));
        });

        // TODO there should be a limit on a single withdrawal amount.
        // For instance, in 10000USD and 10000XRP amm with all liquidity
        // provided by one LP, LP can not withdraw all tokens in USD.
        // Withdrawing 90% in USD is also invalid. Besides the impact
        // on the pool there should be a max threshold for single
        // deposit.

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, Fund::All);
            env(rate(gw, 1.25));
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            // Transfer fee is not charged.
            BEAST_EXPECT(expectLine(env, alice, USD(5000)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0.125)));
            // LP deposits, doesn't pay transfer fee.
            fund(env, gw, {carol}, {USD(2500), BTC(0.0625)}, Fund::Acct);
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22000), BTC(0.55), IOUAmount{110, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(500)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0.0125)));
            // LP withdraws, AMM doesn't pay the transfer fee.
            ammAlice.withdraw(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            ammAlice.expectLPTokens(carol, IOUAmount{0, 0});
            BEAST_EXPECT(expectLine(env, carol, USD(2500)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0.0625)));
        }
    }

    void
    testInvalidFeeVote()
    {
        testcase("Invalid Fee Vote");
        using namespace jtx;

        // Invalid flags
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote(
                std::nullopt,
                1000,
                tfAMMWithdrawAll,
                std::nullopt,
                ter(temINVALID_FLAG));
        });

        // Invalid fee.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote(
                std::nullopt,
                65001,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(ammAlice.expectTradingFee(0));
        });

        // Invalid Account
        testAMM([&](AMM& ammAlice, Env& env) {
            Account bad("bad");
            env.memoize(bad);
            ammAlice.vote(bad, 1000, std::nullopt, seq(1), ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.vote(
                alice, 1000, std::nullopt, std::nullopt, ter(terNO_ACCOUNT));
        });

        // Account is not LP
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote(
                carol,
                1000,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Eight votes fill all voting slots.
        // New vote, new account. Fails since the account has
        // fewer tokens share than in the vote slots.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](int i,
                            std::int16_t tokens,
                            std::optional<ter> ter = std::nullopt) {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, tokens);
                ammAlice.vote(
                    a, 500 * (i + 1), std::nullopt, std::nullopt, ter);
            };
            for (int i = 0; i < 8; ++i)
                vote(i, 10000);
            BEAST_EXPECT(ammAlice.expectTradingFee(2250));
            vote(8, 10000, ter(tecAMM_FAILED_VOTE));
        });
    }

    void
    testFeeVote()
    {
        testcase("Fee Vote");
        using namespace jtx;

        // One vote sets fee to 1%.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote({}, 1000);
            BEAST_EXPECT(ammAlice.expectTradingFee(1000));
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
            BEAST_EXPECT(ammAlice.expectTradingFee(2250));
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
            BEAST_EXPECT(ammAlice.expectTradingFee(2250));
            ammAlice.vote(a, 4500);
            BEAST_EXPECT(ammAlice.expectTradingFee(2750));
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
            BEAST_EXPECT(ammAlice.expectTradingFee(2250));
            vote(8, 20000);
            BEAST_EXPECT(ammAlice.expectTradingFee(2945));
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
            BEAST_EXPECT(ammAlice.expectTradingFee(2750));
            vote(0, 20000);
            BEAST_EXPECT(ammAlice.expectTradingFee(2056));
        });
    }

    void
    testInvalidBid()
    {
        testcase("Invalid Bid");
        using namespace jtx;
        using namespace std::chrono;

        // Invalid flags
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.bid(
                carol,
                0,
                std::nullopt,
                {},
                tfAMMWithdrawAll,
                std::nullopt,
                ter(temINVALID_FLAG));
        });

        // Invalid bid options with [Min,Max]SlotPrice
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                100,
                100,
                {},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_OPTIONS));
        });

        // Invalid Bid price 0
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                0,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                std::nullopt,
                0,
                {},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Invalid Account
        testAMM([&](AMM& ammAlice, Env& env) {
            Account bad("bad");
            env.memoize(bad);
            ammAlice.bid(
                bad,
                std::nullopt,
                100,
                {},
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.bid(
                alice,
                std::nullopt,
                100,
                {},
                std::nullopt,
                std::nullopt,
                ter(terNO_ACCOUNT));
        });

        // Auth account is invalid.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {bob},
                std::nullopt,
                std::nullopt,
                ter(terNO_ACCOUNT));
        });

        // More than four Auth accounts.
        testAMM([&](AMM& ammAlice, Env& env) {
            Account ed("ed");
            Account bill("bill");
            Account scott("scott");
            Account james("james");
            env.fund(XRP(1000), bob, ed, bill, scott, james);
            env.close();
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {bob, ed, bill, scott, james},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_OPTIONS));
        });

        // Bid price exceeds LP owned tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                1000001,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                carol,
                std::nullopt,
                1000001,
                {},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });
    }

    void
    testBid()
    {
        testcase("Bid");
        using namespace jtx;
        using namespace std::chrono;

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
            ammAlice.bid(
                carol,
                std::nullopt,
                120,
                {},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
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
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{12000000, 0}));

            // Initial state, not owned. Default MinSlotPrice.
            ammAlice.bid(carol);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{120, 0}));

            // 1st Interval after close, price for 0th interval.
            ammAlice.bid(bob);
            env.close(seconds(intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 1, IOUAmount{126, 0}));

            // 10th Interval after close, price for 1st interval.
            ammAlice.bid(carol);
            env.close(seconds(10 * intervalDuration + 1));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 10, IOUAmount{252298737, -6}));

            // 20th Interval (expired) after close, price for 11th interval.
            ammAlice.bid(bob);
            env.close(seconds(20 * intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 0, IOUAmount{384912158551263, -12}));

            // 0 Interval.
            ammAlice.bid(carol);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 0, IOUAmount{119996367684391, -12}));
            // ~363.232 tokens burned on bidding fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{1199951677207142, -8}));
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
                    XRP(12000), USD(12000), IOUAmount{1199488908260979, -8}));
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

        // Can't pay into AMM account.
        // Can't pay out since there is no keys
        testAMM([&](AMM& ammAlice, Env& env) {
            env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                ter(tecAMM_DIRECT_PAYMENT));
            env(pay(carol, ammAlice.ammAccount(), USD(10)),
                ter(tecAMM_DIRECT_PAYMENT));
        });
    }

    void
    testBasicPaymentEngine()
    {
        testcase("Basic Payment");
        using namespace jtx;

        // Partial payment ~99.0099USD for 100XRP.
        // Force one path with tfNoRippleDirect.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(jtx::XRP(30000), bob);
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100),
                STAmount(USD, UINT64_C(9900990099009901), -12),
                IOUAmount{10000000, 0}));
            // Initial balance 30,000 + 99.0099009901
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(300990099009901), -10}));
            // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
        });

        // Partial payment ~99.0099USD for 100XRP, use default path.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(jtx::XRP(30000), bob);
            env.close();
            env(pay(bob, carol, USD(100)),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100),
                STAmount(USD, UINT64_C(9900990099009901), -12),
                IOUAmount{10000000, 0}));
            // Initial balance 30,000 + 99.0099009901
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(300990099009901), -10}));
            // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
        });

        // This payment is identical to above. While it has
        // both default path and path, activeStrands has one path.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(jtx::XRP(30000), bob);
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100),
                STAmount(USD, UINT64_C(9900990099009901), -12),
                IOUAmount{10000000, 0}));
            // Initial balance 30,000 + 99.0099009901
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(300990099009901), -10}));
            // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
        });

        // Non-default path (with AMM) has a better quality than default path.
        // The max possible liquidity is taken out of non-default
        // path ~17.5XRP/17.5EUR, 17.5EUR/~17.47USD. The rest
        // is taken from the offer.
        {
            Env env(*this);
            fund(env, gw, {alice, carol}, {USD(30000), EUR(30000)}, Fund::All);
            env.close();
            env.fund(XRP(1000), bob);
            env.close();
            auto ammEUR_XRP = AMM(env, alice, XRP(10000), EUR(10000));
            auto ammUSD_EUR = AMM(env, alice, EUR(10000), USD(10000));
            env(offer(alice, XRP(101), USD(100)), txflags(tfPassive));
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammEUR_XRP.expectBalances(
                XRPAmount(10017526291),
                STAmount(EUR, UINT64_C(9982504373906523), -12),
                IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammUSD_EUR.expectBalances(
                STAmount(USD, UINT64_C(9982534949910309), -12),
                STAmount(EUR, UINT64_C(1001749562609347), -11),
                IOUAmount{10000, 0}));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{Amounts{
                    XRPAmount(17639700),
                    STAmount(USD, UINT64_C(1746505008969044), -14)}}}));
            // Initial 30,000 + 100
            BEAST_EXPECT(expectLine(env, carol, STAmount{USD, 30100}));
            // Initial 1,000 - 17526291(AMM pool) - 83360300(offer) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1000) - XRPAmount{17526291} - XRPAmount{83360300} -
                    txfee(env, 1)));
        }

        // Default path (with AMM) has a better quality than a non-default path.
        // The max possible liquidity is taken out of default
        // path ~17.5XRP/17.5USD. The rest is taken from the offer.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.close();
            env.trust(EUR(2000), alice);
            env.close();
            env(pay(gw, alice, EUR(1000)));
            env(offer(alice, XRP(101), EUR(100)), txflags(tfPassive));
            env.close();
            env(offer(alice, EUR(100), USD(100)), txflags(tfPassive));
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(10017526291),
                STAmount(USD, UINT64_C(9982504373906523), -12),
                IOUAmount{10000000, 0}));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                2,
                {{Amounts{
                      XRPAmount(17670582),
                      STAmount(EUR, UINT64_C(17495626093477), -12)},
                  Amounts{
                      STAmount(EUR, UINT64_C(17495626093477), -12),
                      STAmount(USD, UINT64_C(17495626093477), -12)}}}));
            // Initial 30,000 + 99.99999999999
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3009999999999999), -11}));
            // Initial 1,000 - 10017526291(AMM pool) - 83329418(offer) - 10(tx
            // fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1000) - XRPAmount{17526291} - XRPAmount{83329418} -
                    txfee(env, 1)));
        });

        // Default path with AMM and Order Book offer. AMM is consumed first,
        // remaining amount is consumed by the offer.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(100)}, Fund::Acct);
                env.close();
                env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
                env.close();
                env(pay(alice, carol, USD(200)),
                    sendmax(XRP(200)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9999499987),
                    STAmount(USD, UINT64_C(9999499987998749), -12),
                    IOUAmount{999949998749938, -8}));
                // Initial 30,000 + 200
                BEAST_EXPECT(expectLine(env, carol, STAmount{USD, 30200}));
                // Initial 30,000 - 9,900(AMM pool LP) - 99499987(AMM offer) -
                // - 99499988(offer) - 20(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30000) - XRP(9900) - XRPAmount{99499987} -
                        XRPAmount{99499988} - txfee(env, 2)));
                BEAST_EXPECT(expectOffers(
                    env,
                    bob,
                    1,
                    {{{XRPAmount{500012},
                       STAmount{USD, UINT64_C(5000120012508), -13}}}}));
            },
            std::make_pair(XRP(9900), USD(10100)),
            IOUAmount{999949998749938, -8});

        // Offer crossing
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
                env.close();
                env(offer(bob, USD(100), XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9999000000),
                    STAmount(USD, 10000),
                    IOUAmount{999949998749938, -8}));
                // Initial 1,000 + 100
                BEAST_EXPECT(expectLine(env, bob, STAmount{USD, 1100}));
                // Initial 30,000 - 99(offer) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(99) - txfee(env, 1)));
                env.require(offers(bob, 0));
            },
            std::make_pair(XRP(9900), USD(10100)),
            IOUAmount{999949998749938, -8});

        // Partial offer crossing. Smaller offer is consumed because of
        // the quality limit.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
            env.close();
            env(offer(bob, USD(99), XRP(100)));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10050378153},
                STAmount(USD, UINT64_C(99498743710662), -10),
                IOUAmount{10000000, 0}));
            // Initial 1,000 + 50.1256289338(offer)
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{USD, UINT64_C(10501256289338), -10}));
            // Initial 30,000 - 50378153(AMM offer) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(30000) - XRPAmount{50378153} - txfee(env, 1)));
            BEAST_EXPECT(expectOffers(
                env,
                bob,
                1,
                {{Amounts{
                    STAmount{USD, UINT64_C(488743710662), -10},
                    XRPAmount{49368052}}}}));
        });
    }

    void
    testAMMTokens()
    {
        testcase("AMM Token Pool - AMM with token from another AMM");
        using namespace jtx;

        // AMM with one LPToken from another AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::None);
            AMM ammAMMToken(
                env, alice, EUR(10000), STAmount{ammAlice.lptIssue(), 1000000});
            BEAST_EXPECT(ammAMMToken.expectBalances(
                EUR(10000),
                STAmount(ammAlice.lptIssue(), 1000000),
                IOUAmount{100000, 0}));
        });

        // AMM with two LPTokens from other AMMs.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::None);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env,
                alice,
                STAmount{token1, 1000000},
                STAmount{token2, 1000000});
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000000),
                IOUAmount{1000000, 0}));
        });

        // AMM with two LPTokens from other AMMs.
        // LP deposits/withdraws.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::None);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env,
                alice,
                STAmount{token1, 1000000},
                STAmount{token2, 1000000});
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000000),
                IOUAmount{1000000, 0}));
            ammAMMTokens.deposit(alice, 10000);
            ammAMMTokens.withdraw(alice, 10000);
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000000),
                IOUAmount{1000000, 0}));
        });

        // Offer crossing with two AMM LPtokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            fund(env, gw, {alice, carol}, {EUR(10000)}, Fund::None);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            ammAlice1.deposit(carol, 1000000);
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            env(offer(alice, STAmount{token1, 100}, STAmount{token2, 100}),
                txflags(tfPassive));
            env.close();
            env.require(offers(alice, 1));
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                expectLine(env, alice, STAmount{token1, 10000100}) &&
                expectLine(env, alice, STAmount{token2, 9999900}));
            BEAST_EXPECT(
                expectLine(env, carol, STAmount{token2, 1000100}) &&
                expectLine(env, carol, STAmount{token1, 999900}));
            env.require(offers(alice, 0), offers(carol, 0));
        });

        // Offer crossing with two AMM LPTokens via AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            fund(env, gw, {alice, carol}, {EUR(10000)}, Fund::None);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            ammAlice1.deposit(carol, 1000000);
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env, alice, STAmount{token1, 9900}, STAmount{token2, 10100});
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            env.require(offers(carol, 0));
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 9999),
                STAmount(token2, 10000),
                IOUAmount{999949998749938, -11}));
            // Carol initial token1 1,000,000 - 99(offer)
            BEAST_EXPECT(expectLine(env, carol, STAmount{token1, 999901}));
            // Carol initial token2 1,000,000 + 100(offer)
            BEAST_EXPECT(expectLine(env, carol, STAmount{token2, 1000100}));
        });

        // LPs pay LPTokens directly. Must trust set .
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const token1 = ammAlice.lptIssue();
            env.trust(STAmount{token1, 2000000}, carol);
            env.close();
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(
                ammAlice.expectLPTokens(alice, IOUAmount{10000000, 0}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{1000000, 0}));
            // Pool balance doesn't change, only tokens moved from
            // one line to another.
            env(pay(alice, carol, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                // Alice initial token1 10,000,000 - 100
                ammAlice.expectLPTokens(alice, IOUAmount{9999900, 0}) &&
                // Carol initial token1 1,000,000 + 100
                ammAlice.expectLPTokens(carol, IOUAmount{1000100, 0}));
        });

        // AMM with two tokens from another AMM.
        // LP pays LPTokens to non-LP via AMM.
        // Non-LP must trust set for LPTokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::None);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env,
                alice,
                STAmount{token1, 1000000},
                STAmount{token2, 1000000});
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000000),
                IOUAmount{1000000, 0}));
            env.trust(STAmount{token1, 1000}, carol);
            env.close();
            env(pay(alice, carol, STAmount{token1, 100}),
                path(BookSpec(token1.account, token1.currency)),
                sendmax(STAmount{token2, 100}),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, UINT64_C(9999000099990001), -10),
                STAmount(token2, 1000100),
                IOUAmount{1000000, 0}));
            // Alice's token1 balance doesn't change after the payment.
            // The payment comes out of AMM pool. Alice's token1 balance
            // is initial 10,000,000 - 1,000,000 deposited into ammAMMTokens
            // pool.
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, IOUAmount{9000000}));
            // Carol got ~99.99 token1 from ammAMMTokens pool. Alice swaps
            // in 100 token2 into ammAMMTokens pool.
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount{999900009999, -10}));
            // Alice's token2 balance changes. Initial 10,000,000 - 1,000,000
            // deposited into ammAMMTokens pool - 100 payment.
            BEAST_EXPECT(ammAlice1.expectLPTokens(alice, IOUAmount{8999900}));
        });
    }

    void
    testEnforceNoRipple(FeatureBitset features)
    {
        testcase("Enforce No Ripple");
        using namespace jtx;

        {
            // No ripple with an implied account step after an offer
            Env env{*this, features};

            Account const dan("dan");
            Account const gw1("gw1");
            Account const gw2("gw2");
            auto const USD1 = gw1["USD"];
            auto const USD2 = gw2["USD"];

            env.fund(XRP(20000), alice, noripple(bob), carol, dan, gw1, gw2);
            env.trust(USD1(20000), alice, carol, dan);
            env(trust(bob, USD1(1000), tfSetNoRipple));
            env.trust(USD2(1000), alice, carol, dan);
            env(trust(bob, USD2(1000), tfSetNoRipple));

            env(pay(gw1, dan, USD1(10000)));
            env(pay(gw1, bob, USD1(50)));
            env(pay(gw2, bob, USD2(50)));

            AMM ammDan(env, dan, XRP(10000), USD1(10000));

            env(pay(alice, carol, USD2(50)),
                path(~USD1, bob),
                sendmax(XRP(50)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_DRY));
        }

        {
            // Make sure payment works with default flags
            Env env{*this, features};

            Account const dan("dan");
            Account const gw1("gw1");
            Account const gw2("gw2");
            auto const USD1 = gw1["USD"];
            auto const USD2 = gw2["USD"];

            env.fund(XRP(20000), alice, bob, carol, dan, gw1, gw2);
            env.trust(USD1(20000), alice, bob, carol, dan);
            env.trust(USD2(1000), alice, bob, carol, dan);

            env(pay(gw1, dan, USD1(10000)));
            env(pay(gw1, bob, USD1(50)));
            env(pay(gw2, bob, USD2(50)));

            AMM ammDan(env, dan, XRP(10000), USD1(10000));

            env(pay(alice, carol, USD2(50)),
                path(~USD1, bob),
                sendmax(XRP(60)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(ammDan.expectBalances(
                XRPAmount{10050251257}, USD1(9950), IOUAmount{10000000, 0}));

            env.require(balance(
                alice,
                20000 * dropsPerXRP - XRPAmount{50251257} - txfee(env, 1)));
            env.require(balance(bob, USD1(100)));
            env.require(balance(bob, USD2(0)));
            env.require(balance(carol, USD2(50)));
        }
    }

    void
    testFillModes(FeatureBitset features)
    {
        testcase("Fill Modes");
        using namespace jtx;

        auto const startBalance = XRP(1000000);

        // Fill or Kill - unless we fully cross, just charge a fee and don't
        // place the offer on the books.  But also clean up expired offers
        // that are discovered along the way.
        //
        // fix1578 changes the return code.  Verify expected behavior
        // without and with fix1578.
        for (auto const& tweakedFeatures :
             {features - fix1578, features | fix1578})
        {
            // Order that can't be filled
            {
                Env env{*this, tweakedFeatures};

                auto const f = txfee(env, 1);

                fund(env, gw, {alice, bob}, startBalance);
                env.close();
                env(trust(alice, USD(20000)), ter(tesSUCCESS));
                env.close();
                env(trust(bob, USD(500)), ter(tesSUCCESS));
                env.close();
                env(pay(gw, alice, USD(15000)), ter(tesSUCCESS));
                env.close();
                AMM ammAlice(env, alice, XRP(10000), USD(10000));
                env.close();
                TER const killedCode{
                    tweakedFeatures[fix1578] ? TER{tecKILLED}
                                             : TER{tesSUCCESS}};
                env(offer(bob, USD(500), XRP(500)),
                    txflags(tfFillOrKill),
                    ter(killedCode));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(10000), IOUAmount{10000000, 0}));
                env.require(
                    balance(
                        alice,
                        startBalance - XRP(10000) - (f * 2)),  // trust+AMM
                    owners(bob, 1),
                    offers(bob, 0));
            }

            {
                Env env{*this, tweakedFeatures};

                fund(env, gw, {alice, bob}, startBalance);
                env.close();
                env(trust(alice, USD(20000)), ter(tesSUCCESS));
                env.close();
                env(trust(bob, USD(1000)), ter(tesSUCCESS));
                env.close();
                env(pay(gw, alice, USD(15000)), ter(tesSUCCESS));
                env.close();
                env(pay(gw, bob, USD(500)), ter(tesSUCCESS));
                env.close();
                AMM ammAlice(env, alice, XRP(10000), USD(10000));
                // TODO
                // Order that can be filled has it been another offer
                // instead of AMM. Does this work in practice with AMM?
                // There is no exact match.
#if 0
                env(offer(bob, XRP(500), USD(500)),
                    txflags(tfFillOrKill),
                    ter(tesSUCCESS));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(10000), IOUAmount{10000000, 0}));
                env.require(
                    balance(
                        alice,
                        startBalance - XRP(10000) - (f * 2)),  // trust + AMM
                    owners(bob, 1),
                    offers(bob, 0));
#endif
            }

            // Immediate or Cancel - cross as much as possible
            // and add nothing on the books:
            // Partially cross
            {
                Env env{*this, features};

                auto const f = txfee(env, 1);

                fund(env, gw, {alice, bob}, startBalance);

                env(trust(alice, USD(1000)), ter(tesSUCCESS));
                env(pay(gw, alice, USD(1000)), ter(tesSUCCESS));
                env(trust(bob, USD(20000)), ter(tesSUCCESS));
                env(pay(gw, bob, USD(15000)), ter(tesSUCCESS));

                AMM ammBob(env, bob, XRP(11000), USD(10000));
                env(offer(alice, XRP(1000), USD(1000)),
                    txflags(tfImmediateOrCancel),
                    ter(tesSUCCESS));
                BEAST_EXPECT(ammBob.expectBalances(
                    XRPAmount{10488088482},
                    STAmount{USD, UINT64_C(1048808848140303), -11},
                    IOUAmount{1048808848170152, -8}));

                env.require(
                    // + AMM - (trust + offer) * fee
                    balance(alice, startBalance - f - f + XRPAmount{511911518}),
                    // AMM
                    balance(
                        alice, STAmount{USD, UINT64_C(51191151859697), -11}),
                    owners(alice, 1),
                    offers(alice, 0),
                    // -AMM - (trust + AMM) * fee
                    balance(bob, startBalance - XRP(11000) - f - f),
                    balance(bob, USD(5000)),
                    // USD + LPTokens
                    owners(bob, 2));
            }

            // Fully cross:
            {
                Env env{*this, features};

                auto const f = txfee(env, 1);

                fund(env, gw, {alice, bob}, startBalance);

                env(trust(alice, USD(1000)), ter(tesSUCCESS));
                env(pay(gw, alice, USD(1000)), ter(tesSUCCESS));
                env(trust(bob, USD(20000)), ter(tesSUCCESS));
                env(pay(gw, bob, USD(15000)), ter(tesSUCCESS));

                // Consumed 1000XRP/900USD
                AMM ammBob(env, bob, XRP(11000), USD(9000));
                env(offer(alice, XRP(1000), USD(1000)),
                    txflags(tfImmediateOrCancel),
                    ter(tesSUCCESS));
                BEAST_EXPECT(ammBob.expectBalances(
                    XRP(10000), USD(9900), IOUAmount{99498743710662, -7}));

                env.require(
                    // + AMM - (trust + offer) * fee
                    balance(alice, startBalance - f - f + XRP(1000)),
                    // AMM
                    balance(alice, USD(100)),
                    owners(alice, 1),
                    offers(alice, 0),
                    // -AMM - (trust + AMM) * fee
                    balance(bob, startBalance - XRP(11000) - f - f),
                    balance(bob, USD(6000)),
                    // USD + LPTokens
                    owners(bob, 2));
            }

            // tfPassive -- place the offer without crossing it.
            {
                Env env(*this, features);

                fund(env, gw, {alice, bob}, startBalance);

                env(trust(bob, USD(1000)));
                env.close();

                env(pay(gw, bob, USD(1000)));
                env.close();

                env(trust(alice, USD(20000)));
                env.close();

                env(pay(gw, alice, USD(15000)));
                env.close();
                AMM ammAlice(env, alice, XRP(11000), USD(9000));
                env.close();

                // TODO
                // Does this work in practice with AMM?
                // There is no exact match.
                // bob creates a passive offer that could cross AMM.
                // bob's offer should stay in the ledger.
#if 0
                env(offer(bob, XRP(1000), USD(1000), tfPassive));
                env.close();
                BEAST_EXPECT(expectOffers(
                    env, bob, 1, {{{XRPAmount{1000}, STAmount{USD, 1000}}}}));
#endif
            }

            // tfPassive -- cross only offers of better quality.
            {
                Env env(*this, features);

                fund(env, gw, {alice, bob}, startBalance);

                env(trust(bob, USD(1000)));
                env.close();

                env(pay(gw, bob, USD(1000)));
                env.close();

                env(trust(alice, USD(20000)));
                env.close();

                env(pay(gw, alice, USD(15000)));
                env.close();
                AMM ammAlice(env, alice, XRP(11000), USD(9000));
                env.close();
                env(offer(alice, USD(1101), XRP(900)));
                env.close();

                // bob creates a passive offer.  That offer should cross AMM
                // and leave alice's offer untouched.
                env(offer(bob, XRP(1000), USD(1000), tfPassive));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(9900), IOUAmount{99498743710662, -7}));
                BEAST_EXPECT(expectOffers(env, bob, 0));
                BEAST_EXPECT(expectOffers(env, alice, 1));
            }
        }
    }

    void
    testOfferCrossWithXRP(FeatureBitset features)
    {
        testcase("Offer Crossing with XRP, Normal order");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {bob}, XRP(10000));
        env.fund(XRP(210000), alice);

        env(trust(alice, USD(1000)));
        env(trust(bob, USD(1000)));

        env(pay(gw, alice, alice["USD"](500)));

        AMM ammAlice(env, alice, XRP(150000), USD(50));

        env(offer(bob, USD(1), XRP(4000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount{153061224490}, USD(49), IOUAmount{273861278752583, -8}));

        // Existing offer pays better than this wants.
        // Partially consume existing offer.
        // Pay 1 USD, get 3061224489 Drops.
        auto const xrpConsumed = XRPAmount{3061224490};

        BEAST_EXPECT(expectLine(env, bob, STAmount{USD, 1}));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, bob, XRP(10000) - xrpConsumed - txfee(env, 2)));

        BEAST_EXPECT(expectLine(env, alice, STAmount{USD, 450}));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, alice, XRP(210000) - XRP(150000) - txfee(env, 2)));
    }

    void
    testCurrencyConversionPartial(FeatureBitset features)
    {
        testcase("Currency Conversion: In Parts");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, {USD(20000)}, Fund::All);
        AMM ammAlice(env, alice, XRP(10000), USD(10000));

        // Alice converts USD to XRP which should fail
        // due to PartialPayment.
        env(pay(alice, alice, XRP(600)),
            sendmax(USD(100)),
            ter(tecPATH_PARTIAL));

        // Alice converts USD to XRP, should succeed because
        // we permit partial payment
        env(pay(alice, alice, XRP(600)),
            sendmax(USD(100)),
            txflags(tfPartialPayment));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount{9900990100}, USD(10100), IOUAmount{10000000}));
    }

    void
    testCrossCurrencyStartXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, {USD(10000)}, Fund::All);

        AMM ammAlice(env, alice, XRP(10000), USD(10000));

        env(pay(alice, bob, USD(100)), sendmax(XRP(120)));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount{10101010102}, USD(9900), IOUAmount{10000000}));
        BEAST_EXPECT(expectLine(env, bob, STAmount{USD, 10100}));
    }

    void
    testCrossCurrencyEndXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: End with XRP");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, {USD(10200)}, Fund::All);

        AMM ammAlice(env, alice, XRP(10000), USD(10000));

        env(pay(alice, bob, XRP(100)), sendmax(USD(120)));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(9900),
            STAmount{USD, UINT64_C(101010101010101), -10},
            IOUAmount{10000000}));
    }

    void
    testCrossCurrencyBridged(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Bridged");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const dan = Account{"dan"};
        auto const USD1 = gw1["USD"];
        auto const EUR1 = gw2["EUR"];

        fund(env, gw1, {gw2, alice, bob, carol, dan}, XRP(60000));

        env(trust(alice, USD1(1000)));
        env.close();
        env(trust(bob, EUR1(1000)));
        env.close();
        env(trust(carol, USD1(10000)));
        env.close();
        env(trust(dan, EUR1(1000)));
        env.close();

        env(pay(gw1, alice, alice["USD"](500)));
        env.close();
        env(pay(gw1, carol, carol["USD"](6000)));
        env(pay(gw2, dan, dan["EUR"](400)));
        env.close();

        AMM ammCarol(env, carol, USD1(5000), XRP(50000));

        env(offer(dan, XRP(500), EUR1(50)));
        env.close();

        Json::Value jtp{Json::arrayValue};
        jtp[0u][0u][jss::currency] = "XRP";
        env(pay(alice, bob, EUR1(30)),
            json(jss::Paths, jtp),
            sendmax(USD1(333)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(
            XRP(49700),
            STAmount{USD1, UINT64_C(5030181086519115), -12},
            IOUAmount{158113883008419, -7}));
        BEAST_EXPECT(expectOffers(env, dan, 1, {{Amounts{XRP(200), EUR(20)}}}));
        BEAST_EXPECT(expectLine(env, bob, STAmount{EUR1, 30}));
    }

    void
    testSellFlagBasic(FeatureBitset features)
    {
        testcase("Offer tfSell: Basic Sell");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, XRP(1000), {}, Fund::Acct);
                env(offer(bob, USD(100), XRP(100)), json(jss::Flags, tfSell));
                env.close();
                // There is a slight results difference because
                // of tfSell flag between this test and offer
                // crossing in testBasicPaymentEngine() test.
                // The difference is due to how limitQuality
                // is handled in one-path AMM optimization.
                // In the former test limitQuality doesn't
                // change remainingOut. In this test limitQuality
                // changes remainingOut, which is 1/2 max because
                // of tfSell, to ~100.5USD. This results in
                // slightly larger consumed offer 100.5USD/99.5XRP
                // as opposed to former of 100USD/99XRP.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9999499988),
                    STAmount{USD, UINT64_C(999949998749938), -11},
                    IOUAmount{999949998749938, -8}));
                BEAST_EXPECT(expectOffers(
                    env,
                    bob,
                    1,
                    {{{STAmount{USD, 500012, -6}, XRPAmount{500012}}}}));
                BEAST_EXPECT(expectLine(
                    env, bob, STAmount{USD, UINT64_C(1005000125006196), -13}));
            },
            {{XRP(9900), USD(10100)}},
            IOUAmount{999949998749938, -8},
            0,
            std::nullopt,
            features);
    }

    void
    testSellFlagExceedLimit(FeatureBitset features)
    {
        testcase("Offer tfSell: 2x Sell Exceed Limit");

        using namespace jtx;

        Env env{*this, features};

        auto const starting_xrp =
            XRP(100) + reserve(env, 1) + env.current()->fees().base * 2;

        env.fund(starting_xrp, gw, alice);
        env.fund(XRP(2000), bob);

        env(trust(alice, USD(150)));
        env(trust(bob, USD(4000)));

        env(pay(gw, bob, bob["USD"](2000)));

        AMM ammBob(env, bob, XRP(1000), USD(2000));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env(offer(alice, USD(100), XRP(100)), json(jss::Flags, tfSell));
        BEAST_EXPECT(ammBob.expectBalances(
            XRPAmount(1100000000),
            STAmount{USD, UINT64_C(1818181818181818), -12},
            IOUAmount{1414213562373095, -9}));
        BEAST_EXPECT(expectLine(
            env, alice, STAmount{USD, UINT64_C(1818181818181818), -13}));
        BEAST_EXPECT(expectLedgerEntryRoot(env, alice, XRP(250)));
        BEAST_EXPECT(expectOffers(env, alice, 0));
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue #535: Gateway Cross Currency");

        using namespace jtx;

        Env env{*this, features};

        auto const XTS = gw["XTS"];
        auto const XXX = gw["XXX"];

        auto const starting_xrp =
            XRP(100.1) + reserve(env, 1) + env.current()->fees().base * 2;
        fund(
            env,
            gw,
            {alice, bob},
            starting_xrp,
            {XTS(100), XXX(100)},
            Fund::All);

        AMM ammAlice(env, alice, XTS(100), XXX(100));

        // WS client is used here because the RPC client could not
        // be convinced to pass the build_path argument
        auto wsc = makeWSClient(env.app().config());
        Json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq(bob);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay(bob, bob, bob["XXX"](1));
        payment[jss::tx_json][jss::Sequence] =
            env.current()
                ->read(keylet::account(bob.id()))
                ->getFieldU32(sfSequence);
        payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
        payment[jss::tx_json][jss::SendMax] =
            bob["XTS"](1.5).value().getJson(JsonOptions::none);
        payment[jss::tx_json][jss::Flags] = tfPartialPayment;
        auto jrr = wsc->invoke("submit", payment);
        BEAST_EXPECT(jrr[jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jrr.isMember(jss::jsonrpc) && jrr[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                jrr.isMember(jss::ripplerpc) && jrr[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jrr.isMember(jss::id) && jrr[jss::id] == 5);
        }

        BEAST_EXPECT(ammAlice.expectBalances(
            STAmount(XTS, UINT64_C(101010101010101), -12),
            STAmount{XXX, 99},
            IOUAmount{100}));
        BEAST_EXPECT(
            expectLine(env, bob, STAmount{XTS, UINT64_C(98989898989899), -12}));
        BEAST_EXPECT(expectLine(env, bob, STAmount{XXX, 101}));
    }

    void
    testAmendment()
    {
        testcase("Amendment");
    }

    void
    testTradingFees()
    {
        testcase("Trading Fees");
    }

    void
    testOffers()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        testEnforceNoRipple(all);
        testFillModes(all);
        // testUnfundedCross
        // testNegativeBalance
        testOfferCrossWithXRP(all);
        // testOfferCrossWithLimitOverride
        // testCurrencyConversionIntoDebt
        testCurrencyConversionPartial(all);
        testCrossCurrencyStartXRP(all);
        testCrossCurrencyEndXRP(all);
        testCrossCurrencyBridged(all);
        // testBridgedSecondLegDry
        testSellFlagBasic(all);
        testSellFlagExceedLimit(all);
        testGatewayCrossCurrency(all);
    }

    void
    testAll()
    {
        testInvalidInstance();
        testInstanceCreate();
        testInvalidDeposit();
        testDeposit();
        testInvalidWithdraw();
        testWithdraw();
        testInvalidFeeVote();
        testFeeVote();
        testInvalidBid();
        testBid();
        testInvalidAMMPayment();
        testBasicPaymentEngine();
        testAMMTokens();
    }

    void
    run() override
    {
        testAll();
        testOffers();
    }
};

struct AMM_manual_test : public Test
{
    void
    testFibonacciPerf()
    {
        testcase("Performance Fibonacci");
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
        testFibonacciPerf();
    }
};

BEAST_DEFINE_TESTSUITE(AMM, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMM_manual, tx, ripple);

}  // namespace test
}  // namespace ripple