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
#include <ripple/rpc/impl/RPCHelpers.h>
#include <boost/regex.hpp>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>

#include <chrono>
#include <utility>
#include <vector>

#include <boost/regex.hpp>

namespace ripple {
namespace test {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
// Functions used in debugging
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
#pragma GCC diagnostic pop

/* TODO Path finding duplicate */
/******************************************************************************/
namespace {

void
stpath_append_one(STPath& st, jtx::Account const& account)
{
    st.push_back(STPathElement({account.id(), std::nullopt, std::nullopt}));
}

template <class T>
std::enable_if_t<std::is_constructible<jtx::Account, T>::value>
stpath_append_one(STPath& st, T const& t)
{
    stpath_append_one(st, jtx::Account{t});
}

void
stpath_append_one(STPath& st, STPathElement const& pe)
{
    st.push_back(pe);
}

template <class T, class... Args>
void
stpath_append(STPath& st, T const& t, Args const&... args)
{
    stpath_append_one(st, t);
    if constexpr (sizeof...(args) > 0)
        stpath_append(st, args...);
}

template <class... Args>
void
stpathset_append(STPathSet& st, STPath const& p, Args const&... args)
{
    st.push_back(p);
    if constexpr (sizeof...(args) > 0)
        stpathset_append(st, args...);
}

bool
equal(STAmount const& sa1, STAmount const& sa2)
{
    return sa1 == sa2 && sa1.issue().account == sa2.issue().account;
}

// Issue path element
auto
IPE(Issue const& iss)
{
    return STPathElement(
        STPathElement::typeCurrency | STPathElement::typeIssuer,
        xrpAccount(),
        iss.currency,
        iss.account);
}

}  // namespace

template <class... Args>
STPath
stpath(Args const&... args)
{
    STPath st;
    stpath_append(st, args...);
    return st;
}

template <class... Args>
bool
same(STPathSet const& st1, Args const&... args)
{
    STPathSet st2;
    stpathset_append(st2, args...);
    if (st1.size() != st2.size())
        return false;

    for (auto const& p : st2)
    {
        if (std::find(st1.begin(), st1.end(), p) == st1.end())
            return false;
    }
    return true;
}

/******************************************************************************/

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

template <typename... Amts>
bool
expectLine(
    jtx::Env& env,
    AccountID const& account,
    STAmount const& value,
    Amts const&... amts)
{
    return expectLine(env, account, value, false) &&
        expectLine(env, account, amts...);
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

/* TODO Escrow test duplicate */
/******************************************************************************/

static Json::Value
escrow(AccountID const& account, AccountID const& to, STAmount const& amount)
{
    using namespace jtx;
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowCreate;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    return jv;
}

static Json::Value
finish(AccountID const& account, AccountID const& from, std::uint32_t seq)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowFinish;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[sfOwner.jsonName] = to_string(from);
    jv[sfOfferSequence.jsonName] = seq;
    return jv;
}

std::array<std::uint8_t, 39> constexpr cb1 = {
    {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC,
     0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
     0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95,
     0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

// A PreimageSha256 fulfillments and its associated condition.
std::array<std::uint8_t, 4> const fb1 = {{0xA0, 0x02, 0x80, 0x00}};

/** Set the "FinishAfter" time tag on a JTx */
struct finish_time
{
private:
    NetClock::time_point value_;

public:
    explicit finish_time(NetClock::time_point const& value) : value_(value)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jt) const
    {
        jt.jv[sfFinishAfter.jsonName] = value_.time_since_epoch().count();
    }
};

struct condition
{
private:
    std::string value_;

public:
    explicit condition(Slice cond) : value_(strHex(cond))
    {
    }

    template <size_t N>
    explicit condition(std::array<std::uint8_t, N> c) : condition(makeSlice(c))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jt) const
    {
        jt.jv[sfCondition.jsonName] = value_;
    }
};

struct fulfillment
{
private:
    std::string value_;

public:
    explicit fulfillment(Slice condition) : value_(strHex(condition))
    {
    }

    template <size_t N>
    explicit fulfillment(std::array<std::uint8_t, N> f)
        : fulfillment(makeSlice(f))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jt) const
    {
        jt.jv[sfFulfillment.jsonName] = value_;
    }
};
/******************************************************************************/
/* TODO Payment Channel test duplicate */
/******************************************************************************/
static Json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt)
{
    using namespace jtx;
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelCreate;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv["SettleDelay"] = settleDelay.count();
    jv["PublicKey"] = strHex(pk.slice());
    if (cancelAfter)
        jv["CancelAfter"] = cancelAfter->time_since_epoch().count();
    if (dstTag)
        jv["DestinationTag"] = *dstTag;
    return jv;
}

static Json::Value
chfund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt)
{
    using namespace jtx;
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelFund;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv["Channel"] = to_string(channel);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    if (expiration)
        jv["Expiration"] = expiration->time_since_epoch().count();
    return jv;
}

static Json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance = std::nullopt,
    std::optional<STAmount> const& amount = std::nullopt,
    std::optional<Slice> const& signature = std::nullopt,
    std::optional<PublicKey> const& pk = std::nullopt)
{
    using namespace jtx;
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelClaim;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv["Channel"] = to_string(channel);
    if (amount)
        jv[jss::Amount] = amount->getJson(JsonOptions::none);
    if (balance)
        jv["Balance"] = balance->getJson(JsonOptions::none);
    if (signature)
        jv["Signature"] = strHex(*signature);
    if (pk)
        jv["PublicKey"] = strHex(pk->slice());
    return jv;
}

static uint256
channel(
    AccountID const& account,
    AccountID const& dst,
    std::uint32_t seqProxyValue)
{
    auto const k = keylet::payChan(account, dst, seqProxyValue);
    return k.key;
}

static STAmount
channelBalance(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    if (!slep)
        return XRPAmount{-1};
    return (*slep)[sfBalance];
}

/******************************************************************************/

class Test : public beast::unit_test::suite
{
protected:
    enum class Fund { All, Acct, Gw, None };
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
        if (how == Fund::All || how == Fund::Gw)
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
            env.close();
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

        // Insufficient reserve, XRP/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                XRP(1000) + reserve(env, 3) + env.current()->fees().base * 4;
            env.fund(starting_xrp, gw);
            env.fund(starting_xrp, alice);
            env.trust(USD(2000), alice);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env.close();
            env(offer(alice, XRP(101), USD(100)));
            env(offer(alice, XRP(102), USD(100)));
            AMM ammAlice(
                env, alice, XRP(1000), USD(1000), ter(tecUNFUNDED_AMM));
        }

        // Insufficient reserve, IOU/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 5;
            env.fund(starting_xrp, gw);
            env.fund(starting_xrp, alice);
            env.trust(USD(2000), alice);
            env.trust(EUR(2000), alice);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env(pay(gw, alice, EUR(2000)));
            env.close();
            env(offer(alice, EUR(101), USD(100)));
            env(offer(alice, EUR(102), USD(100)));
            AMM ammAlice(
                env, alice, EUR(1000), USD(1000), ter(tecINSUF_RESERVE_LINE));
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

        // Depositing mismatched token, invalid Asset1In.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Depositing mismatched token, invalid Asset2In.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Depositing mismatched token, Asset1In.issue == Asset2In.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
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

        // Insufficient reserve, XRP/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10000), gw);
            env.fund(XRP(10000), alice);
            env.fund(starting_xrp, carol);
            env.trust(USD(2000), alice);
            env.trust(USD(2000), carol);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env(pay(gw, carol, USD(2000)));
            env.close();
            env(offer(carol, XRP(100), USD(101)));
            env(offer(carol, XRP(100), USD(102)));
            AMM ammAlice(env, alice, XRP(1000), USD(1000));
            ammAlice.deposit(
                carol,
                XRPAmount(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        }

        // Insufficient reserve, IOU/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10000), gw);
            env.fund(XRP(10000), alice);
            env.fund(starting_xrp, carol);
            env.trust(USD(2000), alice);
            env.trust(EUR(2000), alice);
            env.trust(USD(2000), carol);
            env.trust(EUR(2000), carol);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env(pay(gw, alice, EUR(2000)));
            env(pay(gw, carol, USD(2000)));
            env(pay(gw, carol, EUR(2000)));
            env.close();
            env(offer(carol, XRP(100), USD(101)));
            env(offer(carol, XRP(100), USD(102)));
            AMM ammAlice(env, alice, XRP(1000), USD(1000));
            ammAlice.deposit(
                carol,
                XRPAmount(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }
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
            env.close();
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

        // Mismatched token, invalid Asset1Out issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Mismatched token, invalid Asset2Out issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Mismatched token, Asset1Out.issue == Asset2Out.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
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
            env.close();
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
                1001,
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
                ammAlice.vote(a, 50 * (i + 1), std::nullopt, std::nullopt, ter);
            };
            for (int i = 0; i < 8; ++i)
                vote(i, 100);
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
            vote(8, 100, ter(tecAMM_FAILED_VOTE));
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

        // Eight votes fill all voting slots, set fee 0.225%.
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 8; ++i)
            {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, 10000);
                ammAlice.vote(a, 50 * (i + 1));
            }
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
        });

        // Eight votes fill all voting slots, set fee 0.225%.
        // New vote, same account, sets fee 0.275%
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](Account const& a, int i) {
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, 10000);
                ammAlice.vote(a, 50 * (i + 1));
            };
            Account a("0");
            vote(a, 0);
            for (int i = 1; i < 8; ++i)
            {
                Account a(std::to_string(i));
                vote(a, i);
            }
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
            ammAlice.vote(a, 450);
            BEAST_EXPECT(ammAlice.expectTradingFee(275));
        });

        // Eight votes fill all voting slots, set fee 0.225%.
        // New vote, new account, higher vote weight, set higher fee 0.294%
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](int i, std::uint32_t tokens) {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, tokens);
                ammAlice.vote(a, 50 * (i + 1));
            };
            for (int i = 0; i < 8; ++i)
                vote(i, 100);
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
            vote(8, 200);
            BEAST_EXPECT(ammAlice.expectTradingFee(294));
        });

        // Eight votes fill all voting slots, set fee 0.275%.
        // New vote, new account, higher vote weight, set smaller fee 0.244%
        testAMM([&](AMM& ammAlice, Env& env) {
            auto vote = [&](int i, std::uint32_t tokens) {
                Account a(std::to_string(i));
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, tokens);
                ammAlice.vote(a, 50 * (i + 1));
            };
            for (int i = 8; i > 0; --i)
                vote(i, 100);
            BEAST_EXPECT(ammAlice.expectTradingFee(275));
            vote(0, 200);
            BEAST_EXPECT(ammAlice.expectTradingFee(244));
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

        // Invlaid Min/Max combination
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                200,
                100,
                {},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
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

        // Account is not LP
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
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
            // Bid MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, std::nullopt, 135);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 0, IOUAmount{121275, -3}));

            // Bid Min/MaxSlotPrice fails because the computed price is not in
            // range
            ammAlice.bid(
                carol,
                10,
                100,
                {},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
            // Bid Min/MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, 100, 150);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 0, IOUAmount{12733875, -5}));
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
                // Discounted payment
                ammAlice.deposit(carol, USD(100));
                auto tokens = ammAlice.getLPTokensBalance();
                BEAST_EXPECT(
                    ammAlice.expectBalances(XRP(12000), USD(12100), tokens));
                env(pay(carol, bob, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                BEAST_EXPECT(
                    ammAlice.expectBalances(XRP(12100), USD(12000), tokens));
                // Payment with the fee
                env(pay(alice, carol, XRP(100)), path(~XRP), sendmax(USD(110)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000),
                    STAmount{USD, UINT64_C(121010101010101), -10},
                    tokens));
                // Auction slot expired, no discounted fee
                ammAlice.withdraw(
                    carol, STAmount{USD, UINT64_C(10101010101), -10});
                tokens = ammAlice.getLPTokensBalance();
                BEAST_EXPECT(
                    ammAlice.expectBalances(XRP(12000), USD(12100), tokens));
                env.close(seconds(24 * 3600 + 1));
                // clock is parent's based
                env.close();
                env(pay(carol, bob, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(12101010102), USD(12000), tokens));
            },
            std::nullopt,
            1000);
    }

    void
    testInvalidAMMPayment()
    {
        testcase("Invalid AMM Payment");
        using namespace jtx;
        using namespace std::chrono;
        using namespace std::literals::chrono_literals;

        // Can't pay into AMM account.
        // Can't pay out since there is no keys
        testAMM([&](AMM& ammAlice, Env& env) {
            env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                ter(tecNO_PERMISSION));
            env(pay(carol, ammAlice.ammAccount(), USD(10)),
                ter(tecNO_PERMISSION));
        });

        // Can't pay into AMM with escrow.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const seq1 = env.seq(carol);
            env(escrow(carol, ammAlice.ammAccount(), XRP(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();
            env(finish(carol, carol, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ter(tecNO_PERMISSION));
        });

        // Can't pay into AMM with paychan.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const pk = carol.pk();
            auto const settleDelay = 100s;
            auto const chan =
                channel(carol, ammAlice.ammAccount(), env.seq(carol));
            env(create(
                    carol, ammAlice.ammAccount(), XRP(1000), settleDelay, pk),
                ter(tesSUCCESS));
            env(chfund(carol, chan, XRP(1000)), ter(tesSUCCESS));
            auto const reqBal = channelBalance(*env.current(), chan) + XRP(500);
            auto const authAmt = reqBal + XRP(100);
            env(claim(carol, chan, reqBal, authAmt), ter(tecNO_PERMISSION));
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
                STAmount(USD, UINT64_C(9982534949910292), -12),
                STAmount(EUR, UINT64_C(1001749562609347), -11),
                ammUSD_EUR.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{Amounts{
                    XRPAmount(17639700),
                    STAmount(USD, UINT64_C(1746505008970784), -14)}}}));
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
                // 25% transfer fee
                BEAST_EXPECT(ammAlice.expectBalances(
                    EUR(10100), USD(9975), ammAlice.tokens()));
                // Initial 1,000 + 100
                BEAST_EXPECT(expectLine(env, bob, USD(1100)));
                // Initial 1,000 - 100 - 25% transfer fee
                BEAST_EXPECT(expectLine(env, bob, EUR(875)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{EUR(10000), USD(10100)}});

        // Payment and transfer fee
        // Scenario:
        // Dan's offer 200CAN/200GBP
        // AMM 1000GBP/10156.25EUR
        // Ed's offer 200EUR/200USD
        // Bob sends 244.140625CAN to pay 100USD to Carol
        // Payment execution:
        // bob's 244.140625CAN/1.25 = 195.3125CAN -> dan's offer
        // 195.3125CAN/195.3125GBP 195.3125GBP/1.25 = 156.25GBP -> AMM's offer
        // 156.25GBP/156.25EUR 156.25EUR/1.25 = 125EUR -> ed's offer
        // 125EUR/125USD 125USD/1.25 = 100USD paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                Account const ed("ed");
                auto const CAN = gw["CAN"];
                fund(env, gw, {dan}, {CAN(200), GBP(200)}, Fund::Acct);
                fund(env, gw, {ed}, {EUR(200), USD(200)}, Fund::Acct);
                fund(env, gw, {bob}, {CAN(244.140625)}, Fund::Acct);
                env(trust(carol, USD(100)));
                env(rate(gw, 1.25));
                env.close();
                env(offer(dan, CAN(200), GBP(200)));
                env(offer(ed, EUR(200), USD(200)));
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~GBP, ~EUR, ~USD),
                    sendmax(CAN(244.140625)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(expectLine(env, bob, CAN(0)));
                BEAST_EXPECT(expectLine(env, dan, CAN(395.3125), GBP(4.6875)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(10156.25), EUR(10000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, ed, EUR(325), USD(75)));
                BEAST_EXPECT(expectLine(env, carol, USD(100)));
            },
            {{GBP(10000), EUR(10156.25)}});
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
                    env(offer(alice, USD(110), XRP(100)));
                    env.close();

                    // Carol creates a passive offer.  That offer should cross
                    // AMM and leave Alice's offer untouched.
                    env(offer(carol, XRP(100), USD(100), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10900),
                        STAmount{USD, UINT64_C(908256880733945), -11},
                        ammAlice.tokens()));
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
            env, alice, STAmount{USD, UINT64_C(181818181818182), -12}));
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

        // AMM XRP/USD. Alice places USD/XRP offer.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();

                env(offer(carol, USD(100), XRP(100)));
                env.close();

                // AMM pays 25% transfer fee
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(9975), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                BEAST_EXPECT(expectOffers(env, carol, 0));
            },
            {{XRP(10000), USD(10100)}},
            0,
            std::nullopt,
            features);

        // Reverse the order, so the offer in the books is to sell XRP
        // in return for USD.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();

                env(offer(carol, XRP(100), USD(100)));
                env.close();

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000), USD(10100), ammAlice.tokens()));
                // Carol pays 25% transfer fee
                BEAST_EXPECT(expectLine(env, carol, USD(29875)));
                BEAST_EXPECT(expectOffers(env, carol, 0));
            },
            {{XRP(10100), USD(10000)}},
            0,
            std::nullopt,
            features);

        {
            // Bridged crossing.
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

            // AMM pays 25% transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(9975), ammAlice.tokens()));
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

            // AMM pays 25% transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10050), USD(9987.5), ammAlice.tokens()));
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

            // AMM pays 25% transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(9975), ammAlice.tokens()));
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

            // AMM pays 25% transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(9975), ammAlice.tokens()));
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

        // Set up authorized trust line for AMM.
        env(trust(gw, STAmount{Issue{USD.currency, ammAlice.ammAccount()}, 10}),
            txflags(tfSetfAuth));
        env.close();

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

        // Set up authorized trust line for AMM.
        env(trust(gw, STAmount{Issue{USD.currency, ammAlice.ammAccount()}, 10}),
            txflags(tfSetfAuth));
        env.close();

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

        for (auto const& feature : {noAMM, noNumber})
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

    // carol holds gateway AUD, sells gateway AUD for XRP
    // bob will hold gateway AUD
    // alice pays bob gateway AUD using XRP
    void
    via_offers_via_gateway()
    {
        testcase("via gateway");
        using namespace jtx;

        Env env = pathTestEnv();
        auto const AUD = gw["AUD"];
        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        env(rate(gw, 1.1));
        env.trust(AUD(2000), "bob", "carol");
        env(pay(gw, "carol", AUD(51)));
        env.close();
        AMM ammCarol(env, carol, XRP(40), AUD(51));
        env(pay("alice", "bob", AUD(10)), sendmax(XRP(100)), paths(XRP));
        env.close();
        BEAST_EXPECT(
            ammCarol.expectBalances(XRP(51), AUD(40), ammCarol.tokens()));
        BEAST_EXPECT(expectLine(env, bob, AUD(10)));

        auto const result =
            find_paths(env, "alice", "bob", Account("bob")["USD"](25));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    receive_max()
    {
        testcase("Receive max");
        using namespace jtx;
        auto const charlie = Account("charlie");
        {
            // XRP -> IOU receive max
            Env env = pathTestEnv();
            fund(env, gw, {alice, bob, charlie}, {USD(11)}, Fund::All);
            AMM ammCharlie(env, charlie, XRP(10), USD(11));
            auto [st, sa, da] =
                find_paths(env, alice, bob, USD(-1), XRP(1).value());
            BEAST_EXPECT(sa == XRP(1));
            BEAST_EXPECT(equal(da, USD(1)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getCurrency() == USD.currency);
            }
        }
        {
            // IOU -> XRP receive max
            Env env = pathTestEnv();
            fund(env, gw, {alice, bob, charlie}, {USD(11)}, Fund::All);
            AMM ammCharlie(env, charlie, XRP(11), USD(10));
            env.close();
            auto [st, sa, da] =
                find_paths(env, alice, bob, drops(-1), USD(1).value());
            BEAST_EXPECT(sa == USD(1));
            BEAST_EXPECT(equal(da, XRP(1)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() &&
                    pathElem.getIssuerID() == xrpAccount() &&
                    pathElem.getCurrency() == xrpCurrency());
            }
        }
    }

    void
    path_find_01()
    {
        testcase("Path Find: XRP -> XRP and XRP -> IOU");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account G1{"G1"};
        Account G2{"G2"};
        Account G3{"G3"};
        Account M1{"M1"};

        env.fund(XRP(100000), A1);
        env.fund(XRP(10000), A2);
        env.fund(XRP(1000), A3, G1, G2, G3);
        env.fund(XRP(20000), M1);
        env.close();

        env.trust(G1["XYZ"](5000), A1);
        env.trust(G3["ABC"](5000), A1);
        env.trust(G2["XYZ"](5000), A2);
        env.trust(G3["ABC"](5000), A2);
        env.trust(A2["ABC"](1000), A3);
        env.trust(G1["XYZ"](100000), M1);
        env.trust(G2["XYZ"](100000), M1);
        env.trust(G3["ABC"](100000), M1);
        env.close();

        env(pay(G1, A1, G1["XYZ"](3500)));
        env(pay(G3, A1, G3["ABC"](1200)));
        env(pay(G1, M1, G1["XYZ"](25000)));
        env(pay(G2, M1, G2["XYZ"](25000)));
        env(pay(G3, M1, G3["ABC"](25000)));
        env.close();

        AMM ammM1_G1_G2(env, M1, G1["XYZ"](1000), G2["XYZ"](1000));
        AMM ammM1_XRP_G3(env, M1, XRP(10000), G3["ABC"](1000));

        STPathSet st;
        STAmount sa, da;

        {
            auto const& send_amt = XRP(10);
            std::tie(st, sa, da) =
                find_paths(env, A1, A2, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(st.empty());
        }

        {
            // no path should exist for this since dest account
            // does not exist.
            auto const& send_amt = XRP(200);
            std::tie(st, sa, da) = find_paths(
                env, A1, Account{"A0"}, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(st.empty());
        }

        {
            auto const& send_amt = G3["ABC"](10);
            std::tie(st, sa, da) =
                find_paths(env, A2, G3, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{101010102}));
            BEAST_EXPECT(same(st, stpath(IPE(G3["ABC"]))));
        }

        {
            auto const& send_amt = A2["ABC"](1);
            std::tie(st, sa, da) =
                find_paths(env, A1, A2, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{10010011}));
            BEAST_EXPECT(same(st, stpath(IPE(G3["ABC"]), G3)));
        }

        {
            auto const& send_amt = A3["ABC"](1);
            std::tie(st, sa, da) =
                find_paths(env, A1, A3, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{10010011}));
            BEAST_EXPECT(same(st, stpath(IPE(G3["ABC"]), G3, A2)));
        }
    }

    void
    path_find_02()
    {
        testcase("Path Find: non-XRP -> XRP");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account G3{"G3"};
        Account M1{"M1"};

        env.fund(XRP(1000), A1, A2, G3);
        env.fund(XRP(11000), M1);
        env.close();

        env.trust(G3["ABC"](1000), A1, A2);
        env.trust(G3["ABC"](100000), M1);
        env.close();

        env(pay(G3, A1, G3["ABC"](1000)));
        env(pay(G3, A2, G3["ABC"](1000)));
        env(pay(G3, M1, G3["ABC"](1200)));
        env.close();

        AMM ammM1(env, M1, G3["ABC"](1000), XRP(10010));

        STPathSet st;
        STAmount sa, da;

        auto const& send_amt = XRP(10);
        std::tie(st, sa, da) =
            find_paths(env, A1, A2, send_amt, std::nullopt, A2["ABC"].currency);
        BEAST_EXPECT(equal(da, send_amt));
        BEAST_EXPECT(equal(sa, A1["ABC"](1)));
        BEAST_EXPECT(same(st, stpath(G3, IPE(xrpIssue()))));
    }

    void
    path_find_05()
    {
        testcase("Path Find: non-XRP -> non-XRP, same currency");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};
        Account G1{"G1"};
        Account G2{"G2"};
        Account G3{"G3"};
        Account G4{"G4"};
        Account M1{"M1"};
        Account M2{"M2"};

        env.fund(XRP(1000), A1, A2, A3, G1, G2, G3, G4);
        env.fund(XRP(10000), A4);
        env.fund(XRP(21000), M1, M2);
        env.close();

        env.trust(G1["HKD"](2000), A1);
        env.trust(G2["HKD"](2000), A2);
        env.trust(G1["HKD"](2000), A3);
        env.trust(G1["HKD"](100000), M1);
        env.trust(G2["HKD"](100000), M1);
        env.trust(G1["HKD"](100000), M2);
        env.trust(G2["HKD"](100000), M2);
        env.close();

        env(pay(G1, A1, G1["HKD"](1000)));
        env(pay(G2, A2, G2["HKD"](1000)));
        env(pay(G1, A3, G1["HKD"](1000)));
        env(pay(G1, M1, G1["HKD"](1200)));
        env(pay(G2, M1, G2["HKD"](5000)));
        env(pay(G1, M2, G1["HKD"](1200)));
        env(pay(G2, M2, G2["HKD"](5000)));
        env.close();

        AMM ammM1(env, M1, G1["HKD"](1010), G2["HKD"](1000));
        AMM ammM2XRP_G2(env, M2, XRP(10000), G2["HKD"](1010));
        AMM ammM2G1_XRP(env, M2, G1["HKD"](1010), XRP(10000));

        STPathSet st;
        STAmount sa, da;

        {
            // A) Borrow or repay --
            //  Source -> Destination (repay source issuer)
            auto const& send_amt = G1["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, G1, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
        }

        {
            // A2) Borrow or repay --
            //  Source -> Destination (repay destination issuer)
            auto const& send_amt = A1["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, G1, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
        }

        {
            // B) Common gateway --
            //  Source -> AC -> Destination
            auto const& send_amt = A3["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, A3, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(G1)));
        }

        {
            // C) Gateway to gateway --
            //  Source -> OB -> Destination
            auto const& send_amt = G2["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, G1, G2, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, G1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(IPE(G2["HKD"])),
                stpath(M1),
                stpath(M2),
                stpath(IPE(xrpIssue()), IPE(G2["HKD"]))));
        }

        {
            // D) User to unlinked gateway via order book --
            //  Source -> AC -> OB -> Destination
            auto const& send_amt = G2["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, G2, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(G1, M1),
                stpath(G1, M2),
                stpath(G1, IPE(G2["HKD"])),
                stpath(G1, IPE(xrpIssue()), IPE(G2["HKD"]))));
        }

        {
            // I4) XRP bridge" --
            //  Source -> AC -> OB to XRP -> OB from XRP -> AC -> Destination
            auto const& send_amt = A2["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, A2, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(G1, M1, G2),
                stpath(G1, M2, G2),
                stpath(G1, IPE(G2["HKD"]), G2),
                stpath(G1, IPE(xrpIssue()), IPE(G2["HKD"]), G2)));
        }
    }

    void
    path_find_06()
    {
        testcase("Path Find: non-XRP -> non-XRP, same currency)");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account G1{"G1"};
        Account G2{"G2"};
        Account M1{"M1"};

        env.fund(XRP(11000), M1);
        env.fund(XRP(1000), A1, A2, A3, G1, G2);
        env.close();

        env.trust(G1["HKD"](2000), A1);
        env.trust(G2["HKD"](2000), A2);
        env.trust(A2["HKD"](2000), A3);
        env.trust(G1["HKD"](100000), M1);
        env.trust(G2["HKD"](100000), M1);
        env.close();

        env(pay(G1, A1, G1["HKD"](1000)));
        env(pay(G2, A2, G2["HKD"](1000)));
        env(pay(G1, M1, G1["HKD"](5000)));
        env(pay(G2, M1, G2["HKD"](5000)));
        env.close();

        AMM ammM1(env, M1, G1["HKD"](1010), G2["HKD"](1000));

        // E) Gateway to user
        //  Source -> OB -> AC -> Destination
        auto const& send_amt = A2["HKD"](10);
        STPathSet st;
        STAmount sa, da;
        std::tie(st, sa, da) =
            find_paths(env, G1, A2, send_amt, std::nullopt, G1["HKD"].currency);
        BEAST_EXPECT(equal(da, send_amt));
        BEAST_EXPECT(equal(sa, G1["HKD"](10)));
        BEAST_EXPECT(same(st, stpath(M1, G2), stpath(IPE(G2["HKD"]), G2)));
    }

    void
    testPaths()
    {
        path_find_consume_all();
        via_offers_via_gateway();
        receive_max();
        path_find_01();
        path_find_02();
        path_find_05();
        path_find_06();
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

/** AMM Calculator. Uses AMM formulas to simulate the payment engine expected
 * results. Assuming the formulas are correct some unit-tests can be verified.
 * Currently supported operations are:
 * swapIn - find out given in. in can flow through multiple AMM/Offer steps.
 * swapOut - find in given out. out can flow through multiple AMM/Offer steps.
 * lptokens - find lptokens given pool composition
 * changespq - change AMM spot price (SP) quality. given AMM and Offer find out
 *   AMM offer, which changes AMM's SP quality to the Offer's quality.
 */
class AMMCalc_test : public beast::unit_test::suite
{
    using token_iter = boost::sregex_token_iterator;
    using steps = std::vector<std::pair<Amounts, bool>>;
    using trates = std::map<std::string, std::uint32_t>;
    using swapargs = std::tuple<steps, STAmount, trates, std::uint32_t>;
    jtx::Account const gw{jtx::Account("gw")};
    std::optional<STAmount>
    getAmt(token_iter& p, bool* delimeted = nullptr)
    {
        using namespace jtx;
        token_iter end;
        if (p == end)
            return STAmount{};
        std::string str = *p++;
        str = boost::regex_replace(str, boost::regex("^(A|O)[(]"), "");
        boost::smatch match;
        // XXX(val))?
        boost::regex rx("^([^(]+)[(]([^)]+)[)]([)])?$");
        if (boost::regex_search(str, match, rx))
        {
            if (delimeted)
            {
                *delimeted = false;
                if (match[3] != "")
                    *delimeted = true;
            }
            if (match[1] == "XRP")
                return XRP(std::stoll(match[2]));
            // drops
            else if (match[1] == "XRPA")
                return XRPAmount{std::stoll(match[2])};
            return amountFromString(gw[match[1]], match[2]);
        }
        return std::nullopt;
    }

    std::optional<std::tuple<std::string, std::uint32_t, bool>>
    getRate(token_iter& p)
    {
        token_iter end;
        if (p == end)
            return std::nullopt;
        std::string str = *p++;
        str = boost::regex_replace(str, boost::regex("^T[(]"), "");
        // XXX(rate))?
        boost::smatch match;
        boost::regex rx("^([^(]+)[(]([^)]+)[)]([)])?$");
        if (boost::regex_search(str, match, rx))
        {
            std::string const currency = match[1];
            // input is rate * 100, no fraction
            std::uint32_t rate = 10'000'000 * std::stoi(match[2].str());
            // true if delimited - )
            return {{currency, rate, match[3] != "" ? true : false}};
        }
        return std::nullopt;
    }

    std::uint32_t
    getFee(token_iter& p)
    {
        token_iter end;
        if (p != end)
        {
            std::string const s = *p++;
            return std::stoll(s);
        }
        return 0;
    }

    std::optional<std::pair<Amounts, bool>>
    getAmounts(token_iter& p)
    {
        token_iter end;
        if (p == end)
            return std::nullopt;
        std::string const s = *p;
        bool const amm = s[0] == 'O' ? false : true;
        auto const a1 = getAmt(p);
        if (!a1 || p == end)
            return std::nullopt;
        auto const a2 = getAmt(p);
        if (!a2)
            return std::nullopt;
        return {{{*a1, *a2}, amm}};
    }

    std::optional<trates>
    getTransferRate(token_iter& p)
    {
        token_iter end;
        trates rates{};
        if (p == end)
            return rates;
        std::string str = *p;
        if (str[0] != 'T')
            return rates;
        // T(USD(rate),GBP(rate), ...)
        while (true)
        {
            if (auto const rate = getRate(p))
            {
                auto const [currency, trate, delimeted] = *rate;
                rates[currency] = trate;
                if (delimeted)
                    break;
            }
            else
                return std::nullopt;
        }
        return rates;
    }

    std::optional<swapargs>
    getSwap(token_iter& p)
    {
        token_iter end;
        // pairs of amm pool or offer
        steps pairs;
        // either amm pool or offer
        auto isPair = [](auto const& p) {
            std::string const s = *p;
            return s[0] == 'A' || s[0] == 'O';
        };
        // get AMM or offer
        while (isPair(p))
        {
            auto const res = getAmounts(p);
            if (!res || p == end)
                return std::nullopt;
            pairs.push_back(*res);
        }
        // swap in/out amount
        auto const swap = getAmt(p);
        if (!swap)
            return std::nullopt;
        // optional transfer rate
        auto const rate = getTransferRate(p);
        if (!rate)
            return std::nullopt;
        auto const fee = getFee(p);
        return {{pairs, *swap, *rate, fee}};
    }

    std::string
    toString(STAmount const& a)
    {
        std::stringstream str;
        str << a.getText() << "/" << to_string(a.issue().currency);
        return str.str();
    }

    STAmount
    mulratio(STAmount const& amt, std::uint32_t a, std::uint32_t b, bool round)
    {
        if (a == b)
            return amt;
        if (amt.native())
            return toSTAmount(mulRatio(amt.xrp(), a, b, round), amt.issue());
        return toSTAmount(mulRatio(amt.iou(), a, b, round), amt.issue());
    }

    void
    swapOut(swapargs const& args)
    {
        auto const vp = std::get<steps>(args);
        STAmount sout = std::get<STAmount>(args);
        auto const fee = std::get<std::uint32_t>(args);
        auto const rates = std::get<trates>(args);
        STAmount resultOut = sout;
        STAmount resultIn{};
        STAmount sin{};
        int limitingStep = vp.size();
        STAmount limitStepOut{};
        auto trate = [&](auto const& amt) {
            auto const currency = to_string(amt.issue().currency);
            return rates.find(currency) != rates.end() ? rates.at(currency)
                                                       : QUALITY_ONE;
        };
        // swap out reverse
        sin = sout;
        for (auto it = vp.rbegin(); it != vp.rend(); ++it)
        {
            sout = mulratio(sin, trate(sin), QUALITY_ONE, true);
            auto const [amts, amm] = *it;
            // assume no amm limit
            if (amm)
            {
                sin = swapAssetOut(amts, sout, fee);
            }
            else if (sout <= amts.out)
            {
                sin = Quality{amts}.ceil_out(amts, sout).in;
            }
            // limiting step
            else
            {
                sin = amts.in;
                limitingStep = vp.rend() - it - 1;
                limitStepOut = amts.out;
            }
            resultIn = sin;
        }
        sin = limitStepOut;
        // swap in if limiting step
        for (int i = limitingStep + 1; i < vp.size(); ++i)
        {
            auto const [amts, amm] = vp[i];
            sin = mulratio(sin, QUALITY_ONE, trate(sin), false);
            if (amm)
            {
                sout = swapAssetIn(amts, sin, fee);
            }
            // assume there is no limiting step in fwd
            else
            {
                sout = Quality{amts}.ceil_in(amts, sin).out;
            }
            sin = sout;
            resultOut = sout;
        }
        std::cout << "in: " << toString(resultIn)
                  << " out: " << toString(resultOut) << std::endl;
    }

    void
    swapIn(swapargs const& args)
    {
        auto const vp = std::get<steps>(args);
        STAmount sin = std::get<STAmount>(args);
        auto const fee = std::get<std::uint32_t>(args);
        auto const rates = std::get<trates>(args);
        STAmount resultIn = sin;
        STAmount resultOut{};
        STAmount sout{};
        int limitingStep = 0;
        STAmount limitStepIn{};
        auto trate = [&](auto const& amt) {
            auto const currency = to_string(amt.issue().currency);
            return rates.find(currency) != rates.end() ? rates.at(currency)
                                                       : QUALITY_ONE;
        };
        // Swap in forward
        for (auto it = vp.begin(); it != vp.end(); ++it)
        {
            auto const [amts, amm] = *it;
            sin = mulratio(
                sin, QUALITY_ONE, trate(sin), false);  // out of the next step
            // assume no amm limit
            if (amm)
            {
                sout = swapAssetIn(amts, sin, fee);
            }
            else if (sin <= amts.in)
            {
                sout = Quality{amts}.ceil_in(amts, sin).out;
            }
            // limiting step, requested in is greater than the offer
            // pay exactly amts.in, which gets amts.out
            else
            {
                sout = amts.out;
                limitingStep = it - vp.begin();
                limitStepIn = amts.in;
            }
            sin = sout;
            resultOut = sout;
        }
        sin = limitStepIn;
        // swap out if limiting step
        for (int i = limitingStep - 1; i >= 0; --i)
        {
            sout = mulratio(sin, trate(sin), QUALITY_ONE, false);
            auto const [amts, amm] = vp[i];
            if (amm)
            {
                sin = swapAssetOut(amts, sout, fee);
            }
            // assume there is no limiting step
            else
            {
                sin = Quality{amts}.ceil_out(amts, sout).in;
            }
            resultIn = sin;
        }
        resultOut = mulratio(resultOut, QUALITY_ONE, trate(resultOut), true);
        std::cout << "in: " << toString(resultIn)
                  << " out: " << toString(resultOut) << std::endl;
    }

    void
    run() override
    {
        using namespace jtx;
        auto const a = arg();
        boost::regex re(",");
        token_iter p(a.begin(), a.end(), re, -1);
        // AMM must be in the order poolGets/poolPays
        // Offer must be in the order takerPays/takerGets
        auto const res = [&]() -> bool {
            // Swap in to the pool
            // swapin,A(USD(1000),XRP(1000)),T(USD(125)),XRP(10),10 -
            //   steps,trates,fee
            // steps are comma separated A():AMM or O():Offer
            // trates and fee are optional
            // trates is comma separated rate for each currency
            // trate is 100 * rate, no fraction
            if (*p == "swapin")
            {
                if (auto const swap = getSwap(++p); swap)
                {
                    swapIn(*swap);
                    return true;
                }
            }
            // Swap out of the pool
            // swapout,A(USD(1000),XRP(1000)),T(USD(125)),XRP(10),10 -
            //   steps,trates,fee
            // steps are comma separated A():AMM or O():Offer
            // trates and fee are optional
            // trates is comma separated rate for each currency
            // trate is 100 * rate, no fraction
            else if (*p == "swapout")
            {
                if (auto const swap = getSwap(++p); swap)
                {
                    swapOut(*swap);
                    return true;
                }
            }
            // Pool's lptokens
            // lptokens,USD(1000),XRP(1000)
            else if (*p == "lptokens")
            {
                if (auto const pool = getAmounts(++p); pool)
                {
                    Account const amm("amm");
                    auto const LPT = amm["LPT"];
                    std::cout
                        << to_string(
                               ammLPTokens(pool->first.in, pool->first.out, LPT)
                                   .iou())
                        << std::endl;
                    return true;
                }
            }
            // Change spot price quality
            // changespq,A(XRP(1000),USD(1000)),O(XRP(100),USD(99)),10 -
            //   AMM,Offer,fee
            else if (*p == "changespq")
            {
                if (auto const pool = getAmounts(++p))
                {
                    if (auto const offer = getAmounts(p))
                    {
                        auto const fee = getFee(p);
                        if (auto const ammOffer = changeSpotPriceQuality(
                                pool->first, Quality{offer->first}, fee);
                            ammOffer)
                            std::cout
                                << "amm offer: " << toString(ammOffer->in)
                                << " " << toString(ammOffer->out)
                                << "\nnew pool: "
                                << toString(pool->first.in + ammOffer->in)
                                << " "
                                << toString(pool->first.out - ammOffer->out)
                                << std::endl;
                        else
                            std::cout << "can't change the pool's SP quality"
                                      << std::endl;
                        return true;
                    }
                }
            }
            return false;
        }();
        BEAST_EXPECT(res);
    }
};

BEAST_DEFINE_TESTSUITE(AMM, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMMCalc, app, ripple);

}  // namespace test
}  // namespace ripple