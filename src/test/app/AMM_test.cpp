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
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
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
#endif

static XRPAmount
txfee(jtx::Env const& env, std::uint16_t n)
{
    return env.current()->fees().base * n;
}

static bool
expectLine(
    jtx::Env& env,
    AccountID const& account,
    STAmount const& value,
    bool defaultTrustline = false)
{
    if (auto const sle = env.le(keylet::line(account, value.issue())))
    {
        Issue const issue = value.issue();
        bool const accountLow = account < issue.account;

        bool expectDefaultTrustLine = true;
        if (defaultTrustline)
        {
            STAmount low{issue};
            STAmount high{issue};

            low.setIssuer(accountLow ? account : issue.account);
            high.setIssuer(accountLow ? issue.account : account);

            expectDefaultTrustLine = sle->getFieldAmount(sfLowLimit) == low &&
                sle->getFieldAmount(sfHighLimit) == high;
        }

        auto amount = sle->getFieldAmount(sfBalance);
        amount.setIssuer(value.issue().account);
        if (!accountLow)
            amount.negate();
        return amount == value && expectDefaultTrustLine;
    }
    return false;
}

static bool
expectLine(jtx::Env& env, AccountID const& account, jtx::None const& value)
{
    return !env.le(keylet::line(account, value.issue));
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

static bool
equal(STAmount const& sa1, STAmount const& sa2)
{
    return sa1 == sa2 && sa1.issue().account == sa2.issue().account;
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

    /** testAMM() funds 30,000XRP and 30,000IOU
     * for each non-XRP asset to Alice and Carol
     */
    template <typename F>
    void
    testAMM(
        F&& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::optional<FeatureBitset> const& features = std::nullopt)
    {
        using namespace jtx;
        auto env = features ? Env{*this, *features} : Env{*this};

        auto const [asset1, asset2] =
            pool ? *pool : std::make_pair(XRP(10000), USD(10000));
        auto tofund = [&](STAmount const& a) -> STAmount {
            if (a.native())
                return XRP(30000);
            return STAmount{a.issue(), 30000};
        };
        auto const toFund1 = tofund(asset1);
        auto const toFund2 = tofund(asset2);
        assert(asset1 <= toFund1 && asset2 <= toFund2);

        if (!asset1.native() && !asset2.native())
            fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
        else if (asset1.native())
            fund(env, gw, {alice, carol}, {toFund2}, Fund::All);
        else if (asset2.native())
            fund(env, gw, {alice, carol}, {toFund1}, Fund::All);

        AMM ammAlice(env, alice, asset1, asset2, false, fee);
        BEAST_EXPECT(
            ammAlice.expectBalances(asset1, asset2, ammAlice.tokens()));
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

    /* TODO: code duplication with Path_test
     ***********************************************/
    class gate
    {
    private:
        std::condition_variable cv_;
        std::mutex mutex_;
        bool signaled_ = false;

    public:
        // Thread safe, blocks until signaled or period expires.
        // Returns `true` if signaled.
        template <class Rep, class Period>
        bool
        wait_for(std::chrono::duration<Rep, Period> const& rel_time)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, rel_time, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        void
        signal()
        {
            std::lock_guard lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    jtx::Env
    pathTestEnv()
    {
        // These tests were originally written with search parameters that are
        // different from the current defaults. This function creates an env
        // with the search parameters that the tests were written for.
        using namespace jtx;
        return Env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->PATH_SEARCH_OLD = 7;
            cfg->PATH_SEARCH = 7;
            cfg->PATH_SEARCH_MAX = 10;
            return cfg;
        }));
    }

    auto
    find_paths_request(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt)
    {
        using namespace jtx;

        auto& app = env.app();
        Resource::Charge loadType = Resource::feeReferenceRPC;
        Resource::Consumer c;

        RPC::JsonContext context{
            {env.journal,
             app,
             loadType,
             app.getOPs(),
             app.getLedgerMaster(),
             c,
             Role::USER,
             {},
             {},
             RPC::apiVersionIfUnspecified},
            {},
            {}};

        Json::Value params = Json::objectValue;
        params[jss::command] = "ripple_path_find";
        params[jss::source_account] = toBase58(src);
        params[jss::destination_account] = toBase58(dst);
        params[jss::destination_amount] =
            saDstAmount.getJson(JsonOptions::none);
        if (saSendMax)
            params[jss::send_max] = saSendMax->getJson(JsonOptions::none);
        if (saSrcCurrency)
        {
            auto& sc = params[jss::source_currencies] = Json::arrayValue;
            Json::Value j = Json::objectValue;
            j[jss::currency] = to_string(saSrcCurrency.value());
            sc.append(j);
        }

        Json::Value result;
        gate g;
        app.getJobQueue().postCoro(
            jtCLIENT, "RPC-Client", [&](auto const& coro) {
                context.params = std::move(params);
                context.coro = coro;
                RPC::doCommand(context, result);
                g.signal();
            });

        using namespace std::chrono_literals;
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(!result.isMember(jss::error));
        return result;
    }

    std::tuple<STPathSet, STAmount, STAmount>
    find_paths(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt)
    {
        Json::Value result = find_paths_request(
            env, src, dst, saDstAmount, saSendMax, saSrcCurrency);
        BEAST_EXPECT(!result.isMember(jss::error));

        STAmount da;
        if (result.isMember(jss::destination_amount))
            da = amountFromJson(sfGeneric, result[jss::destination_amount]);

        STAmount sa;
        STPathSet paths;
        if (result.isMember(jss::alternatives))
        {
            auto const& alts = result[jss::alternatives];
            if (alts.size() > 0)
            {
                auto const& path = alts[0u];

                if (path.isMember(jss::source_amount))
                    sa = amountFromJson(sfGeneric, path[jss::source_amount]);

                if (path.isMember(jss::destination_amount))
                    da = amountFromJson(
                        sfGeneric, path[jss::destination_amount]);

                if (path.isMember(jss::paths_computed))
                {
                    Json::Value p;
                    p["Paths"] = path[jss::paths_computed];
                    STParsedJSONObject po("generic", p);
                    paths = po.object->getFieldPathSet(sfPaths);
                }
            }
        }

        return std::make_tuple(std::move(paths), std::move(sa), std::move(da));
    }
    /************************************************/
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
            {{USD(20000), BTC(0.5)}});

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
                env, alice, XRP(10000), USD(40000), ter(tecUNFUNDED_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient XRP balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecUNFUNDED_AMM));
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
            AMM ammAlice(env, alice, XRP(10000), USD(10000), ter(tecNO_AUTH));
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
            BEAST_EXPECT(ammAlice.expectTradingFee(2445));
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

        // Payment 100USD for 100XRP.
        // Force one path with tfNoRippleDirect.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(tfNoRippleDirect));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10000), USD(10100)}});

        // Payment 100USD for 100XRP, use default path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)), sendmax(XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10000), USD(10100)}});

        // This payment is identical to above. While it has
        // both default path and path, activeStrands has one path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)), path(~USD), sendmax(XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10000), USD(10100)}});

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
                ammEUR_XRP.tokens()));
            BEAST_EXPECT(ammUSD_EUR.expectBalances(
                STAmount(USD, UINT64_C(9982534949910309), -12),
                STAmount(EUR, UINT64_C(1001749562609347), -11),
                ammUSD_EUR.tokens()));
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
                ammAlice.tokens()));
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
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial 30,000 + 200
                BEAST_EXPECT(expectLine(env, carol, USD(30200)));
                // Initial 30,000 - 10000(AMM pool LP) - 100(AMM offer) -
                // - 100(offer) - 20(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30000) - XRP(10000) - XRP(100) - XRP(100) -
                        txfee(env, 2)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10000), USD(10100)}});

        // Offer crossing XRP/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
                env.close();
                env(offer(bob, USD(100), XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial 1,000 + 100
                BEAST_EXPECT(expectLine(env, bob, USD(1100)));
                // Initial 30,000 - 100(offer) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10000), USD(10100)}});

        // Offer crossing IOU/IOU and transfer rate
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(1000), EUR(1000)}, Fund::Acct);
                env(rate(gw, 1.25));
                env.close();
                env(offer(bob, USD(100), EUR(100)));
                env.close();
                // transfer fee is not charged
                BEAST_EXPECT(ammAlice.expectBalances(
                    EUR(10100), USD(10000), ammAlice.tokens()));
                // Initial 1,000 + 100
                BEAST_EXPECT(expectLine(env, bob, USD(1100)));
                // Initial 1,000 - 100
                BEAST_EXPECT(expectLine(env, bob, EUR(900)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{EUR(10000), USD(10100)}});
    }

    void
    testAMMTokens()
    {
        testcase("AMM Token Pool - AMM with token(s) from another AMM");
        using namespace jtx;

        // AMM with one LPToken from another AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::None);
            AMM ammAMMToken(
                env, alice, EUR(10000), STAmount{ammAlice.lptIssue(), 1000000});
            BEAST_EXPECT(ammAMMToken.expectBalances(
                EUR(10000),
                STAmount(ammAlice.lptIssue(), 1000000),
                ammAMMToken.tokens()));
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
                ammAMMTokens.tokens()));
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
                ammAMMTokens.tokens()));
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
            BEAST_EXPECT(expectOffers(env, alice, 1));
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                expectLine(env, alice, STAmount{token1, 10000100}) &&
                expectLine(env, alice, STAmount{token2, 9999900}));
            BEAST_EXPECT(
                expectLine(env, carol, STAmount{token2, 1000100}) &&
                expectLine(env, carol, STAmount{token1, 999900}));
            BEAST_EXPECT(
                expectOffers(env, alice, 0) && expectOffers(env, carol, 0));
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
                env, alice, STAmount{token1, 10000}, STAmount{token2, 10100});
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 10100),
                STAmount(token2, 10000),
                ammAMMTokens.tokens()));
            // Carol initial token1 1,000,000 - 100(offer)
            BEAST_EXPECT(expectLine(env, carol, STAmount{token1, 999900}));
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
                STAmount{token1, 1000100},
                STAmount{token2, 1000000});
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000100),
                STAmount(token2, 1000000),
                ammAMMTokens.tokens()));
            env.trust(STAmount{token1, 1000}, carol);
            env.close();
            env(pay(alice, carol, STAmount{token1, 100}),
                path(BookSpec(token1.account, token1.currency)),
                sendmax(STAmount{token2, 100}),
                txflags(tfNoRippleDirect));
            env.close();
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000100),
                ammAMMTokens.tokens()));
            // Alice's token1 balance doesn't change after the payment.
            // The payment comes out of AMM pool. Alice's token1 balance
            // is initial 10,000,000 - 1,000,100 deposited into ammAMMTokens
            // pool.
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, IOUAmount{8999900}));
            // Carol got 100 token1 from ammAMMTokens pool. Alice swaps
            // in 100 token2 into ammAMMTokens pool.
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{100}));
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
            // No ripple with an implied account step after AMM
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
                XRPAmount{10050251257}, USD1(9950), ammDan.tokens()));

            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                alice,
                20000 * dropsPerXRP - XRPAmount{50251257} - txfee(env, 1)));
            BEAST_EXPECT(expectLine(env, bob, USD1(100)));
            BEAST_EXPECT(expectLine(env, bob, USD2(0)));
            BEAST_EXPECT(expectLine(env, carol, USD2(50)));
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
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    TER const killedCode{
                        tweakedFeatures[fix1578] ? TER{tecKILLED}
                                                 : TER{tesSUCCESS}};
                    env(offer(carol, USD(100), XRP(100)),
                        txflags(tfFillOrKill),
                        ter(killedCode));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10000), USD(10000), ammAlice.tokens()));
                    // fee = AMM
                    BEAST_EXPECT(expectLedgerEntryRoot(
                        env, carol, XRP(30000) - (txfee(env, 1))));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                    BEAST_EXPECT(expectLine(env, carol, USD(30000)));
                },
                std::nullopt,
                0,
                std::nullopt,
                tweakedFeatures);

            // Order that can be filled
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    env(offer(carol, XRP(100), USD(100)),
                        txflags(tfFillOrKill),
                        ter(tesSUCCESS));
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10000), USD(10100), ammAlice.tokens()));
                    BEAST_EXPECT(expectLedgerEntryRoot(
                        env, carol, XRP(30000) + XRP(100) - txfee(env, 1)));
                    BEAST_EXPECT(expectLine(env, carol, USD(29900)));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                },
                {{XRP(10100), USD(10000)}},
                0,
                std::nullopt,
                tweakedFeatures);

            // Immediate or Cancel - cross as much as possible
            // and add nothing on the books.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    env(offer(carol, XRP(100), USD(100)),
                        txflags(tfImmediateOrCancel),
                        ter(tesSUCCESS));

                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10000), USD(10100), ammAlice.tokens()));
                    // +AMM - offer * fee
                    BEAST_EXPECT(expectLedgerEntryRoot(
                        env, carol, XRP(30000) + XRP(100) - txfee(env, 1)));
                    // AMM
                    BEAST_EXPECT(expectLine(env, carol, USD(29900)));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                },
                {{XRP(10100), USD(10000)}},
                0,
                std::nullopt,
                tweakedFeatures);

            // tfPassive -- place the offer without crossing it.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    // Carol creates a passive offer that could cross AMM.
                    // Carol's offer should stay in the ledger.
                    env(offer(carol, XRP(100), USD(100), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10100), STAmount{USD, 10000}, ammAlice.tokens()));
                    BEAST_EXPECT(expectOffers(
                        env, carol, 1, {{{XRP(100), STAmount{USD, 100}}}}));
                },
                {{XRP(10100), USD(10000)}},
                0,
                std::nullopt,
                tweakedFeatures);

            // tfPassive -- cross only offers of better quality.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    env(offer(alice, USD(1101), XRP(900)));
                    env.close();

                    // Carol creates a passive offer.  That offer should cross
                    // AMM and leave Alice's offer untouched.
                    env(offer(carol, XRP(1000), USD(1000), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10000), USD(9900), ammAlice.tokens()));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                    BEAST_EXPECT(expectOffers(env, alice, 1));
                },
                {{XRP(11000), USD(9000)}},
                0,
                std::nullopt,
                tweakedFeatures);
        }
    }

    void
    testOfferCrossWithXRP(FeatureBitset features)
    {
        testcase("Offer Crossing with XRP, Normal order");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {bob, alice}, XRP(300000), {USD(100)}, Fund::All);

        AMM ammAlice(env, alice, XRP(150000), USD(50));

        env(offer(bob, USD(1), XRP(4000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount{153061224490}, USD(49), IOUAmount{273861278752583, -8}));

        // Existing offer pays better than this wants.
        // Partially consume existing offer.
        // Pay 1 USD, get 3061224490 Drops.
        auto const xrpConsumed = XRPAmount{3061224490};

        BEAST_EXPECT(expectLine(env, bob, STAmount{USD, 101}));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, bob, XRP(300000) - xrpConsumed - txfee(env, 1)));
        BEAST_EXPECT(expectOffers(env, bob, 0));
    }

    void
    testCurrencyConversionPartial(FeatureBitset features)
    {
        testcase("Currency Conversion: In Parts");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Alice converts USD to XRP which should fail
                // due to PartialPayment.
                env(pay(alice, alice, XRP(100)),
                    sendmax(USD(100)),
                    ter(tecPATH_PARTIAL));

                // Alice converts USD to XRP, should succeed because
                // we permit partial payment
                env(pay(alice, alice, XRP(100)),
                    sendmax(USD(100)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{9900990100}, USD(10100), ammAlice.tokens()));
                // initial 30,000 - 10,000AMM - 100pay
                BEAST_EXPECT(expectLine(env, alice, USD(19900)));
                // initial 30,000 - 10,0000AMM + 99.009900pay - fee*3
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30000) - XRP(10000) + XRPAmount{99009900} -
                        txfee(env, 3)));
            },
            {{XRP(10000), USD(10000)}},
            0,
            std::nullopt,
            features);
    }

    void
    testCrossCurrencyStartXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(XRP(1000), bob);
                env(trust(bob, USD(100)));
                env.close();
                env(pay(alice, bob, USD(100)), sendmax(XRP(100)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, bob, USD(100)));
            },
            {{XRP(10000), USD(10100)}},
            0,
            std::nullopt,
            features);
    }

    void
    testCrossCurrencyEndXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: End with XRP");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(XRP(1000), bob);
                env(trust(bob, USD(100)));
                env.close();
                env(pay(alice, bob, XRP(100)), sendmax(USD(100)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(10100), ammAlice.tokens()));
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(1000) + XRP(100) - txfee(env, 1)));
            },
            {{XRP(10100), USD(10000)}},
            0,
            std::nullopt,
            features);
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
            ammCarol.tokens()));
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
                env(offer(carol, USD(100), XRP(100)), json(jss::Flags, tfSell));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(9999), ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectLine(env, carol, USD(30101)));
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, carol, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(9900), USD(10100)}},
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
            XRP(1100),
            STAmount{USD, UINT64_C(1818181818181818), -12},
            ammBob.tokens()));
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
            XXX(99),
            ammAlice.tokens()));
        BEAST_EXPECT(
            expectLine(env, bob, STAmount{XTS, UINT64_C(98989898989899), -12}));
        BEAST_EXPECT(expectLine(env, bob, XXX(101)));
    }

    void
    testBridgedCross(FeatureBitset features)
    {
        testcase("Bridged Crossing");

        using namespace jtx;

        {
            Env env{*this, features};

            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15000), EUR(15000)},
                Fund::All);

            // The scenario:
            //   o USD/XPR AMM is created.
            //   o EUR/XRP AMM is created.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10000), USD(10100));
            AMM ammBob(env, bob, EUR(10000), XRP(10100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(10000), EUR(10100), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15100)));
            BEAST_EXPECT(expectLine(env, carol, EUR(14900)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        {
            Env env{*this, features};

            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15000), EUR(15000)},
                Fund::All);

            // The scenario:
            //   o USD/XPR AMM is created.
            //   o EUR/XRP offer is created.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and bob's offer are created, then autobridging
            // will not occur.
            AMM ammAlice(env, alice, XRP(10000), USD(10100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15100)));
            BEAST_EXPECT(expectLine(env, carol, EUR(14900)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            Env env{*this, features};

            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15000), EUR(15000)},
                Fund::All);

            // The scenario:
            //   o USD/XPR offer is created.
            //   o EUR/XRP AMM is created.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and alice's offer are created, then
            // autobridging will not occur.
            env(offer(alice, XRP(100), USD(100)));
            env.close();
            AMM ammBob(env, bob, EUR(10000), XRP(10100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(10000), EUR(10100), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15100)));
            BEAST_EXPECT(expectLine(env, carol, EUR(14900)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
    }

    void
    testSellWithFillOrKill(FeatureBitset features)
    {
        // Test a number of different corner cases regarding offer crossing
        // when both the tfSell flag and tfFillOrKill flags are set.
        testcase("Combine tfSell with tfFillOrKill");

        using namespace jtx;

        // Code returned if an offer is killed.
        TER const killedCode{
            features[fix1578] ? TER{tecKILLED} : TER{tesSUCCESS}};

        {
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(20000)}, Fund::All);
            AMM ammBob(env, bob, XRP(20000), USD(200));
            // alice submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice, USD(2.1), XRP(210), tfSell | tfFillOrKill),
                ter(killedCode));

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(20000), USD(200), ammBob.tokens()));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }
        {
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(1000)}, Fund::All);
            AMM ammBob(env, bob, XRP(20000), USD(200));
            // alice submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(alice, USD(2), XRP(220), tfSell | tfFillOrKill));
            env.close();
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(20220),
                STAmount{USD, UINT64_C(1978239366963402), -13},
                ammBob.tokens()));
            BEAST_EXPECT(expectLine(
                env, alice, STAmount{USD, UINT64_C(100217606330366), -11}));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(1000)}, Fund::All);
            AMM ammBob(env, bob, XRP(20000), USD(200));

            env(offer(alice, USD(10), XRP(1500), tfSell | tfFillOrKill));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(21500),
                STAmount{USD, UINT64_C(186046511627907), -12},
                ammBob.tokens()));
            BEAST_EXPECT(expectLine(
                env, alice, STAmount{USD, UINT64_C(1013953488372093), -12}));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that doesn't cross.
            // This would have succeeded with a regular tfSell, but the
            // fillOrKill prevents the transaction from crossing since not
            // all of the offer is consumed.
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(10000)}, Fund::All);
            AMM ammBob(env, bob, XRP(500), USD(5));

            env(offer(alice, USD(1), XRP(501), tfSell | tfFillOrKill),
                ter(killedCode));
            env.close();
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(500), USD(5), ammBob.tokens()));
        }
    }

    void
    testTransferRateOffer(FeatureBitset features)
    {
        testcase("Transfer Rate Offer");

        using namespace jtx;

        // Transfer fee is not charged if AMM is src/dst.
        // AMM XRP/USD. Alice places USD/XRP offer. The transfer fee is not
        // charged.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();

                env(offer(carol, USD(100), XRP(100)));
                env.close();

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                BEAST_EXPECT(expectOffers(env, carol, 0));
            },
            {{XRP(10000), USD(10100)}},
            0,
            std::nullopt,
            features);

        // Reverse the order, so the offer in the books is to sell XRP
        // in return for USD. The transfer fee is not charged.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();

                env(offer(carol, XRP(100), USD(100)));
                env.close();

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(10100), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, carol, USD(29900)));
                BEAST_EXPECT(expectOffers(env, carol, 0));
            },
            {{XRP(10100), USD(10000)}},
            0,
            std::nullopt,
            features);

        {
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15000), EUR(15000)},
                Fund::All);
            env(rate(gw, 1.25));

            // The scenario:
            //   o USD/XPR AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10000), USD(10100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15100)));
            // Carol pays 25% transfer fee.
            BEAST_EXPECT(expectLine(env, carol, EUR(14875)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15000), EUR(15000)},
                Fund::All);
            env(rate(gw, 1.25));

            // The scenario:
            //   o USD/XPR AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10000), USD(10050));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // partially consumes Bob's offer.
            env(offer(carol, USD(50), EUR(50)));
            env.close();
            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10050), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15050)));
            // Carol pays 25% transfer fee.
            BEAST_EXPECT(expectLine(env, carol, EUR(14937.5)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(
                expectOffers(env, bob, 1, {{Amounts{EUR(50), XRP(50)}}}));
        }

        {
            // A trust line's QualityIn should not affect offer crossing.
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw, {alice, carol, bob}, XRP(30000));
            env(rate(gw, 1.25));
            env(trust(alice, USD(15000)));
            env(trust(bob, EUR(15000)));
            env(trust(carol, EUR(15000)), qualityInPercent(80));
            env(trust(bob, USD(15000)));
            env(trust(carol, USD(15000)));
            env.close();

            env(pay(gw, alice, USD(11000)));
            env(pay(gw, carol, EUR(1000)), sendmax(EUR(10000)));
            env.close();
            // 1000 / 0.8
            BEAST_EXPECT(expectLine(env, carol, EUR(1250)));
            // The scenario:
            //   o USD/XPR AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10000), USD(10100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(100)));
            // Carol pays 25% transfer fee: 1250 - 100(offer) - 25(transfer fee)
            BEAST_EXPECT(expectLine(env, carol, EUR(1125)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            // A trust line's QualityOut should not affect offer crossing.
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw, {alice, carol, bob}, XRP(30000));
            env(rate(gw, 1.25));
            env(trust(alice, USD(15000)));
            env(trust(bob, EUR(15000)));
            env(trust(carol, EUR(15000)), qualityOutPercent(120));
            env(trust(bob, USD(15000)));
            env(trust(carol, USD(15000)));
            env.close();

            env(pay(gw, alice, USD(11000)));
            env(pay(gw, carol, EUR(1000)), sendmax(EUR(10000)));
            env.close();
            BEAST_EXPECT(expectLine(env, carol, EUR(1000)));
            // The scenario:
            //   o USD/XPR AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10000), USD(10100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(100)));
            // Carol pays 25% transfer fee: 1000 - 100(offer) - 25(transfer fee)
            BEAST_EXPECT(expectLine(env, carol, EUR(875)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }
    }

    void
    testSelfIssueOffer(FeatureBitset features)
    {
        // This test is not the same as corresponding testSelfIssueOffer()
        // in the Offer_test. It simply tests AMM with self issue and
        // offer crossing.
        using namespace jtx;

        Env env{*this, features};

        auto const USD_bob = bob["USD"];
        auto const f = env.current()->fees().base;

        env.fund(XRP(30000) + f, alice, bob);
        env.close();
        AMM ammBob(env, bob, XRP(10000), USD_bob(10100));

        env(offer(alice, USD_bob(100), XRP(100)));
        env.close();

        BEAST_EXPECT(
            ammBob.expectBalances(XRP(10100), USD_bob(10000), ammBob.tokens()));
        BEAST_EXPECT(expectOffers(env, alice, 0));
        BEAST_EXPECT(expectLine(env, alice, USD_bob(100)));
    }

    void
    testBadPathAssert(FeatureBitset features)
    {
        // At one point in the past this invalid path caused assert.  It
        // should not be possible for user-supplied data to cause assert.
        // Make sure assert is gone.
        testcase("Bad path assert");

        using namespace jtx;

        // The problem was identified when featureOwnerPaysFee was enabled,
        // so make sure that gets included.
        Env env{*this, features | featureOwnerPaysFee};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;
        {
            // A trust line's QualityOut should not affect offer crossing.
            auto const ann = Account("ann");
            auto const A_BUX = ann["BUX"];
            auto const bob = Account("bob");
            auto const cam = Account("cam");
            auto const dan = Account("dan");
            auto const D_BUX = dan["BUX"];

            // Verify trust line QualityOut affects payments.
            env.fund(reserve(env, 4) + (fee * 4), ann, bob, cam, dan);
            env.close();

            env(trust(bob, A_BUX(400)));
            env(trust(bob, D_BUX(200)), qualityOutPercent(120));
            env(trust(cam, D_BUX(100)));
            env.close();
            env(pay(dan, bob, D_BUX(100)));
            env.close();
            BEAST_EXPECT(expectLine(env, bob, D_BUX(100)));

            env(pay(ann, cam, D_BUX(60)), path(bob, dan), sendmax(A_BUX(200)));
            env.close();

            BEAST_EXPECT(expectLine(env, ann, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, ann, D_BUX(none)));
            BEAST_EXPECT(expectLine(env, bob, A_BUX(72)));
            BEAST_EXPECT(expectLine(env, bob, D_BUX(40)));
            BEAST_EXPECT(expectLine(env, cam, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, cam, D_BUX(60)));
            BEAST_EXPECT(expectLine(env, dan, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, dan, D_BUX(none)));

            AMM ammBob(env, bob, A_BUX(30), D_BUX(30));

            env(trust(ann, D_BUX(100)));
            env.close();

            // This payment caused the assert.
            env(pay(ann, ann, D_BUX(30)),
                path(A_BUX, D_BUX),
                sendmax(A_BUX(30)),
                ter(temBAD_PATH));
            env.close();

            BEAST_EXPECT(
                ammBob.expectBalances(A_BUX(30), D_BUX(30), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, ann, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, ann, D_BUX(0)));
            BEAST_EXPECT(expectLine(env, cam, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, cam, D_BUX(60)));
            BEAST_EXPECT(expectLine(env, dan, A_BUX(0)));
            BEAST_EXPECT(expectLine(env, dan, D_BUX(none)));
        }
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("lsfRequireAuth");

        using namespace jtx;

        Env env{*this, features};

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        env.fund(XRP(400000), gw, alice, bob);
        env.close();

        // GW requires authorization for holders of its IOUs
        env(fset(gw, asfRequireAuth));
        env.close();

        // Properly set trust and have gw authorize bob and alice
        env(trust(gw, bobUSD(100)), txflags(tfSetfAuth));
        env(trust(bob, USD(100)));
        env(trust(gw, aliceUSD(100)), txflags(tfSetfAuth));
        env(trust(alice, USD(2000)));
        env(pay(gw, alice, USD(1000)));
        env.close();
        // Alice is able to create AMM since the GW has authorized her
        AMM ammAlice(env, alice, USD(1000), XRP(1050));

        env(pay(gw, bob, USD(50)));
        env.close();

        BEAST_EXPECT(expectLine(env, bob, USD(50)));

        // Bob's offer should cross Alice's AMM
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        BEAST_EXPECT(
            ammAlice.expectBalances(USD(1050), XRP(1000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob, 0));
        BEAST_EXPECT(expectLine(env, bob, USD(0)));
    }

    void
    testMissingAuth(FeatureBitset features)
    {
        testcase("Missing Auth");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(400000), gw, alice, bob);
        env.close();

        // Alice doesn't have the funds
        {
            AMM ammAlice(
                env, alice, USD(1000), XRP(1000), ter(tecUNFUNDED_AMM));
        }

        env(fset(gw, asfRequireAuth));
        env.close();

        env(trust(gw, bob["USD"](50)), txflags(tfSetfAuth));
        env.close();
        env(trust(bob, USD(50)));
        env.close();

        env(pay(gw, bob, USD(50)));
        env.close();
        BEAST_EXPECT(expectLine(env, bob, USD(50)));

        // Alice should not be able to create AMM without authorization.
        {
            AMM ammAlice(env, alice, USD(1000), XRP(1000), ter(tecNO_LINE));
        }

        // Set up a trust line for Alice, but don't authorize it. Alice
        // should still not be able to create AMM for USD/gw.
        env(trust(gw, alice["USD"](2000)));
        env.close();

        {
            AMM ammAlice(env, alice, USD(1000), XRP(1000), ter(tecNO_AUTH));
        }

        // Finally, set up an authorized trust line for Alice. Now Alice's
        // AMM create should succeed.
        env(trust(gw, alice["USD"](100)), txflags(tfSetfAuth));
        env(trust(alice, USD(2000)));
        env(pay(gw, alice, USD(1000)));
        env.close();

        AMM ammAlice(env, alice, USD(1000), XRP(1050));

        // Now bob creates his offer again, which crosses with  alice's AMM.
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        BEAST_EXPECT(
            ammAlice.expectBalances(USD(1050), XRP(1000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob, 0));
        BEAST_EXPECT(expectLine(env, bob, USD(0)));
    }

    void
    testAmendment()
    {
        testcase("Amendment");
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const noAMM{all - featureAMM};
        FeatureBitset const noNumber{all - fixUniversalNumber};
        FeatureBitset const noFlowCross{all - featureFlowCross};

        for (auto const& feature : {noAMM, noNumber, noFlowCross})
        {
            Env env{*this, feature};
            fund(env, gw, {alice}, {USD(1000)}, Fund::All);
            AMM amm(env, alice, XRP(1000), USD(1000), ter(temDISABLED));
        }
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
        testBridgedCross(all);
        testSellWithFillOrKill(all);
        testTransferRateOffer(all);
        testSelfIssueOffer(all);
        testBadPathAssert(all);
        testRequireAuth(all);
        testMissingAuth(all);
        // testRCSmoketest
        // testDeletedOfferIssuer
    }

    void
    path_find_consume_all()
    {
        testcase("path find consume all");
        using namespace jtx;

        Env env = pathTestEnv();
        fund(env, gw, {alice, carol, bob}, {USD(100)}, Fund::All);
        AMM ammCarol(env, carol, XRP(100), USD(100));

        STPathSet st;
        STAmount sa;
        STAmount da;
        std::tie(st, sa, da) = find_paths(
            env,
            alice,
            bob,
            bob["AUD"](-1),
            std::optional<STAmount>(XRP(100000000)));
        BEAST_EXPECT(st.empty());
        std::tie(st, sa, da) = find_paths(
            env,
            alice,
            bob,
            bob["USD"](-1),
            std::optional<STAmount>(XRP(100000000)));
        BEAST_EXPECT(sa == XRP(100));
        BEAST_EXPECT(equal(da, bob["USD"](100)));
    }

    void
    testPaths()
    {
        path_find_consume_all();
    }

    void
    testCore()
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
        testAmendment();
    }

    void
    run() override
    {
        testCore();
        testOffers();
        testPaths();
    }
};

BEAST_DEFINE_TESTSUITE(AMM, app, ripple);

}  // namespace test
}  // namespace ripple