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
#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/AMMLiquidity.h>
#include <ripple/app/paths/AMMOffer.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/StrandFlow.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <boost/regex.hpp>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/PathSet.h>
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
getAccountOffers(jtx::Env& env, AccountID const& acct)
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    return env.rpc("json", "account_offers", to_string(jv))[jss::result];
}

static Json::Value
getAccountLines(jtx::Env& env, AccountID const& acctId)
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return env.rpc("json", "account_lines", to_string(jv))[jss::result];
}

template <typename... IOU>
static Json::Value
getAccountLines(jtx::Env& env, AccountID const& acctId, IOU... ious)
{
    auto const jrr = getAccountLines(env, acctId);
    Json::Value res;
    for (auto const& line : jrr["lines"])
    {
        for (auto const& iou : {ious...})
        {
            if (line["currency"].asString() == to_string(iou.currency))
            {
                Json::Value v;
                v["currency"] = line["currency"];
                v["balance"] = line["balance"];
                v["limit"] = line["limit"];
                v["account"] = line["account"];
                res[jss::lines].append(v);
            }
        }
    }
    if (!res.isNull())
        return res;
    return jrr;
}

static bool
checkArraySize(Json::Value const& val, unsigned int size)
{
    return val.isArray() && val.size() == size;
}

static std::uint32_t
ownersCnt(jtx::Env& env, jtx::Account const& id)
{
    return env.le(id)->getFieldU32(sfOwnerCount);
}
#pragma GCC diagnostic pop

/* Path finding */
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

static jtx::PrettyAmount
xrpMinusFee(jtx::Env const& env, std::int64_t xrpAmount)
{
    using namespace jtx;
    auto feeDrops = env.current()->fees().base;
    return drops(dropsPerXRP * xrpAmount - feeDrops);
};

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

static auto
ledgerEntryState(
    jtx::Env& env,
    jtx::Account const& acct_a,
    jtx::Account const& acct_b,
    std::string const& currency)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::ripple_state][jss::currency] = currency;
    jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
    jvParams[jss::ripple_state][jss::accounts].append(acct_a.human());
    jvParams[jss::ripple_state][jss::accounts].append(acct_b.human());
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

static auto
accountBalance(jtx::Env& env, jtx::Account const& acct)
{
    auto const jrr = ledgerEntryRoot(env, acct);
    return jrr[jss::node][sfBalance.fieldName];
}

static bool
expectLedgerEntryRoot(
    jtx::Env& env,
    jtx::Account const& acct,
    STAmount const& expectedValue)
{
    return accountBalance(env, acct) == to_string(expectedValue.xrp());
}

/* Escrow */
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

/* Payment Channel */
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

/* Crossing Limits */
/******************************************************************************/

static void
n_offers(
    jtx::Env& env,
    std::size_t n,
    jtx::Account const& account,
    STAmount const& in,
    STAmount const& out)
{
    using namespace jtx;
    auto const ownerCount = env.le(account)->getFieldU32(sfOwnerCount);
    for (std::size_t i = 0; i < n; i++)
    {
        env(offer(account, in, out));
        env.close();
    }
    env.require(owners(account, ownerCount + n));
}

/* Pay Strand */
/***************************************************************/

// Currency path element
static STPathElement
cpe(Currency const& c)
{
    return STPathElement(
        STPathElement::typeCurrency, xrpAccount(), c, xrpAccount());
};

// All path element
static STPathElement
allpe(AccountID const& a, Issue const& iss)
{
    return STPathElement(
        STPathElement::typeAccount | STPathElement::typeCurrency |
            STPathElement::typeIssuer,
        a,
        iss.currency,
        iss.account);
};

/***************************************************************/

class Test : public jtx::AMMTest
{
public:
    Test() : jtx::AMMTest()
    {
    }

protected:
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

    XRPAmount
    ammCrtFee(jtx::Env& env)
    {
        return env.current()->fees().increment;
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
            // 25,000 - 20,000(AMM) - 0.25*20,000=5,000(fee) = 0
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            // 0.625 - 0.5(AMM) - 0.25*0.5=0.125(fee) = 0
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
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
                env, alice, XRP(10000), XRP(10000), ter(temAMM_BAD_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have both tokens the same IOU
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, USD(10000), USD(10000), ter(temAMM_BAD_TOKENS));
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
                env, alice, XRP(10000), USD(40000), ter(tecAMM_UNFUNDED));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient XRP balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecAMM_UNFUNDED));
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
                10,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // AMM already exists
        testAMM([&](AMM& ammAlice, Env& env) {
            AMM ammCarol(env, carol, XRP(10000), USD(10000), ter(tecDUPLICATE));
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
                10,
                tfWithdrawAll,
                std::nullopt,
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
                10,
                std::nullopt,
                seq(1),
                std::nullopt,
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
                env, alice, XRP(1000), USD(1000), ter(tecAMM_UNFUNDED));
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

        // Insufficient fee
        {
            Env env(*this);
            fund(env, gw, {alice}, XRP(2000), {USD(2000), EUR(2000)});
            AMM ammAlice(
                env,
                alice,
                EUR(1000),
                USD(1000),
                false,
                0,
                ammCrtFee(env).drops() - 1,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(telINSUF_FEE_P));
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
                tfWithdrawAll,
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
                    std::nullopt,
                    ter(temMALFORMED));
            });
        }

        // Invalid tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice, 0, std::nullopt, std::nullopt, ter(temAMM_BAD_TOKENS));
        });

        // Depositing mismatched token, invalid Asset1In.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
        });

        // Depositing mismatched token, invalid Asset2In.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
        });

        // Depositing mismatched token, Asset1In.issue == Asset2In.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
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
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.deposit(
                alice, 10000, std::nullopt, std::nullopt, ter(terNO_AMM));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                std::nullopt,
                ter(terNO_AMM));
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

        // Frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            ammAlice.deposit(
                carol, 1000000, std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Insufficient XRP balance
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.close();
            // Adds LPT trustline
            ammAlice.deposit(bob, XRP(10));
            ammAlice.deposit(
                bob,
                XRP(1000),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_UNFUNDED));
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
                ter(tecAMM_UNFUNDED));
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
                std::nullopt,
                ter(tecAMM_UNFUNDED));
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
                std::nullopt,
                ter(tecAMM_UNFUNDED));
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
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
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
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }

        // Single deposit: 100000 tokens worth of USD
        // Amount to deposit exceeds Max
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol,
                100000,
                USD(200),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
        });

        // Single deposit: 100000 tokens worth of XRP
        // Amount to deposit exceeds Max
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol,
                100000,
                XRP(200),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
        });

        // Deposit amount is invalid
        testAMM([&](AMM& ammAlice, Env&) {
            // Calculated amount to deposit is 98,000,000
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                STAmount{USD, 1, -1},
                std::nullopt,
                ter(tecAMM_UNFUNDED));
            // Calculated amount is 0
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                STAmount{USD, 2000, -6},
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
        });

        // Tiny deposit
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol,
                IOUAmount{1, -4},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol,
                STAmount{USD, 1, -11},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
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
            ammAlice.deposit(carol, 100000, USD(205));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10201), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, XRP(205));
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
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
            fund(env, gw, {carol}, {USD(2500), BTC(0.0625)}, Fund::Acct);
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22000), BTC(0.55), IOUAmount{110, 0}));
            // 2,500 - 2,000(AMM) - 0.25*2,000=500(fee)=0
            BEAST_EXPECT(expectLine(env, carol, USD(0)));
            // 0.0625 - 0.05(AMM) - 0.25*0.05=0.0125(fee)=0
            BEAST_EXPECT(expectLine(env, carol, BTC(0)));
        }

        // Tiny deposits
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, IOUAmount{1, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10000000001},
                STAmount{USD, UINT64_C(10000000001), -6},
                IOUAmount{10000000001, -3}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1, -3}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRPAmount{1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10000000001},
                USD(10000),
                IOUAmount{100000000005, -4}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{5, -4}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, STAmount{USD, 1, -10});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(100000000000001), -10},
                IOUAmount{1000000000000005, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{5, -8}));
        });
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
                tfBurnable,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));
        });

        // Invalid options
        std::vector<std::tuple<
            std::optional<std::uint32_t>,
            std::optional<STAmount>,
            std::optional<STAmount>,
            std::optional<IOUAmount>,
            std::optional<std::uint32_t>,
            NotTEC>>
            invalidOptions = {
                // tokens, asset1Out, asset2Out, EPrice, flags, ter
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 temMALFORMED},
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 tfSingleAsset | tfTwoAsset,
                 temMALFORMED},
                {1000,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 tfWithdrawAll,
                 temMALFORMED},
                {std::nullopt,
                 USD(0),
                 XRP(100),
                 std::nullopt,
                 tfWithdrawAll | tfLPToken,
                 temMALFORMED},
                {std::nullopt,
                 std::nullopt,
                 USD(100),
                 std::nullopt,
                 tfWithdrawAll,
                 temMALFORMED},
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 tfWithdrawAll | tfOneAssetWithdrawAll,
                 temMALFORMED},
                {std::nullopt,
                 USD(100),
                 std::nullopt,
                 std::nullopt,
                 tfWithdrawAll,
                 temMALFORMED},
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 tfOneAssetWithdrawAll,
                 temMALFORMED},
                {1000,
                 std::nullopt,
                 USD(100),
                 std::nullopt,
                 std::nullopt,
                 temMALFORMED},
                {std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 IOUAmount{250, 0},
                 tfWithdrawAll,
                 temMALFORMED},
                {1000,
                 std::nullopt,
                 std::nullopt,
                 IOUAmount{250, 0},
                 std::nullopt,
                 temMALFORMED},
                {std::nullopt,
                 std::nullopt,
                 USD(100),
                 IOUAmount{250, 0},
                 std::nullopt,
                 temMALFORMED},
                {std::nullopt,
                 XRP(100),
                 USD(100),
                 IOUAmount{250, 0},
                 std::nullopt,
                 temMALFORMED},
                {1000,
                 XRP(100),
                 USD(100),
                 std::nullopt,
                 std::nullopt,
                 temMALFORMED},
                {std::nullopt,
                 XRP(100),
                 USD(100),
                 std::nullopt,
                 tfWithdrawAll,
                 temMALFORMED}};
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
                    std::nullopt,
                    ter(std::get<5>(it)));
            });
        }

        // Invalid tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice, 0, std::nullopt, std::nullopt, ter(temAMM_BAD_TOKENS));
        });

        // Mismatched token, invalid Asset1Out issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
        });

        // Mismatched token, invalid Asset2Out issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
        });

        // Mismatched token, Asset1Out.issue == Asset2Out.issue
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
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
                    tfOneAssetWithdrawAll,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
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
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.withdraw(
                alice, 10000, std::nullopt, std::nullopt, ter(terNO_AMM));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                std::nullopt,
                ter(terNO_AMM));
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
                carol, 1000, std::nullopt, std::nullopt, ter(tecFROZEN));
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
                ter(tecAMM_FAILED_WITHDRAW));
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

        // Deposit/Withdraw the same amount with the trading fee
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, USD(1000));
                ammAlice.withdraw(
                    carol,
                    USD(1000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
            },
            std::nullopt,
            1000);
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, XRP(1000));
                ammAlice.withdraw(
                    carol,
                    XRP(1000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
            },
            std::nullopt,
            1000);

        // Tiny withdraw
        testAMM([&](AMM& ammAlice, Env&) {
            // XRP amount to withdraw is 0
            ammAlice.withdraw(
                alice,
                IOUAmount{1, -5},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            // Calculated tokens to withdraw are 0
            ammAlice.withdraw(
                alice,
                std::nullopt,
                STAmount{USD, 1, -11},
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            ammAlice.deposit(carol, STAmount{USD, 1, -10});
            ammAlice.withdraw(
                carol,
                std::nullopt,
                STAmount{USD, 1, -9},
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            ammAlice.withdraw(
                carol,
                std::nullopt,
                XRPAmount{1},
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
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
        // Note round-off on USD
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

        // Single deposit/withdrawal
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            ammAlice.deposit(carol, STAmount(USD, 1, -6));
            ammAlice.withdraw(carol, STAmount(USD, 1, -6));
            ammAlice.deposit(carol, XRPAmount(1));
            ammAlice.withdraw(carol, XRPAmount(1));
            auto const roundoff = IOUAmount{1, -8};
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000} + roundoff));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, roundoff));
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
        });

        // Single deposit by different accounts and then withdraw
        // in reverse. There is a round-off error. There remains
        // a dust amount of tokens.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.deposit(alice, USD(1000));
            ammAlice.withdraw(alice, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            auto const roundoff = IOUAmount{1, -8};
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000} + roundoff));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, roundoff));
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
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
                ammAlice.expectLPTokens(carol, IOUAmount{15384615384615, -8}));
            ammAlice.withdrawAll(carol);
            ammAlice.expectLPTokens(carol, IOUAmount{0});
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
                ammAlice.expectLPTokens(carol, IOUAmount{15384615384615, -8}));
        });

        // TODO there should be a limit on a single withdraw amount.
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
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
            fund(env, gw, {carol}, {USD(2500), BTC(0.0625)}, Fund::Acct);
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22000), BTC(0.55), IOUAmount{110, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0)));
            // LP withdraws, AMM doesn't pay the transfer fee.
            ammAlice.withdraw(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            ammAlice.expectLPTokens(carol, IOUAmount{0, 0});
            // 2,500 - 0.25*2,000=500(deposit fee)=2,000
            BEAST_EXPECT(expectLine(env, carol, USD(2000)));
            // 0.0625 - 0.025*0.5=0.0125(deposit fee)=0.05
            BEAST_EXPECT(expectLine(env, carol, BTC(0.05)));
        }

        // Tiny withdraw
        testAMM([&](AMM& ammAlice, Env&) {
            // By tokens
            ammAlice.withdraw(alice, IOUAmount{1, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9999999999},
                STAmount{USD, UINT64_C(9999999999), -6},
                IOUAmount{9999999999, -3}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            // Single XRP pool
            ammAlice.withdraw(alice, std::nullopt, XRPAmount{1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9999999999}, USD(10000), IOUAmount{99999999995, -4}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            // Single USD pool
            ammAlice.withdraw(alice, std::nullopt, STAmount{USD, 1, -10});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(99999999999999), -10},
                IOUAmount{999999999999995, -8}));
        });
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
                tfWithdrawAll,
                std::nullopt,
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
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(ammAlice.expectTradingFee(0));
        });

        // Invalid Account
        testAMM([&](AMM& ammAlice, Env& env) {
            Account bad("bad");
            env.memoize(bad);
            ammAlice.vote(
                bad,
                1000,
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.vote(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_AMM));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                ter(terNO_AMM));
        });

        // Account is not LP
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.vote(
                carol,
                1000,
                std::nullopt,
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
                    a,
                    50 * (i + 1),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter);
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
                tfWithdrawAll,
                std::nullopt,
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
                std::nullopt,
                ter(temBAD_AMOUNT));
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
                std::nullopt,
                ter(temBAD_AMOUNT));
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
                std::nullopt,
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
                std::nullopt,
                ter(terNO_AMM));
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
                std::nullopt,
                ter(temMALFORMED));
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
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                carol,
                std::nullopt,
                1000001,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Invalid Assets
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.bid(
                alice,
                std::nullopt,
                100,
                {},
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                ter(terNO_AMM));
        });

        // Invalid Min/Max issue
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.bid(
                alice,
                std::nullopt,
                STAmount{USD, 100},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
            ammAlice.bid(
                alice,
                STAmount{USD, 100},
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temAMM_BAD_TOKENS));
        });
    }

    void
    testBid()
    {
        testcase("Bid");
        using namespace jtx;
        using namespace std::chrono;

        // Bid 100 tokens. The slot is not owned, pay bidMin.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));
            // 100 tokens are burned.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{10999890, 0}));
        });

        // Start bid at bidMin 110. The slot is not owned.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            // Bid, pay bidMin.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));

            fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
            ammAlice.deposit(bob, 1000000);
            // Bid, pay the computed price.
            ammAlice.bid(bob);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));

            // Bid bidMax fails because the computed price is higher.
            ammAlice.bid(
                carol,
                std::nullopt,
                120,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
            // Bid MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, std::nullopt, 600);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));

            // Bid Min/MaxSlotPrice fails because the computed price is not in
            // range
            ammAlice.bid(
                carol,
                10,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
            // Bid Min/MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, 100, 600);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));
        });

        // Slot states.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto constexpr intervalDuration = 24 * 3600 / 20;
            ammAlice.deposit(carol, 1000000);

            fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
            ammAlice.deposit(bob, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{12000000, 0}));

            // Initial state, not owned. Pay bidMin.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));

            // 1st Interval after close, price for 0th interval.
            ammAlice.bid(bob);
            env.close(seconds(intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 1, 0));

            // 10th Interval after close, price for 1st interval.
            ammAlice.bid(carol);
            env.close(seconds(10 * intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 10, 1));

            // 20th Interval (expired) after close, price for 10th interval.
            ammAlice.bid(bob);
            env.close(seconds(20 * intervalDuration + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, std::nullopt, 10));

            // 0 Interval.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, std::nullopt));
            // ~307.939 tokens burnt on bidding fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{119996920611875, -7}));
        });

        // Pool's fee 1%. Bid bidMin.
        // Auction slot owner and auth account trade at discounted fee (0).
        // Other accounts trade at 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                fund(env, gw, {bob, dan}, {USD(10000)}, Fund::Acct);
                ammAlice.deposit(bob, 1000000);
                ammAlice.deposit(carol, 500000);
                ammAlice.deposit(dan, 500000);
                ammAlice.bid(carol, 120, std::nullopt, {bob});
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000), USD(12000), IOUAmount{11999880}));
                // Discounted trade
                for (int i = 0; i < 10; ++i)
                {
                    ammAlice.deposit(carol, USD(100));
                    ammAlice.withdraw(carol, USD(100));
                    ammAlice.deposit(bob, USD(100));
                    ammAlice.withdraw(bob, USD(100));
                }
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000), USD(12000), IOUAmount{11999880}));
                auto const danTokens = ammAlice.getLPTokensBalance(dan);
                auto const ammTokens = ammAlice.getLPTokensBalance();
                // Trade with the fee
                for (int i = 0; i < 10; ++i)
                {
                    ammAlice.deposit(dan, USD(100));
                    ammAlice.withdraw(dan, USD(100));
                }
                auto const danFees =
                    danTokens - ammAlice.getLPTokensBalance(dan);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(12000), USD(12000), ammTokens - danFees));
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

        // Bid tiny amount
        testAMM([&](AMM& ammAlice, Env&) {
            // Can bid a tiny amount
            auto const tiny = Number{STAmount::cMinValue, STAmount::cMinOffset};
            ammAlice.bid(alice, IOUAmount{tiny});
            // Auction slot purchase price is equal to the tiny amount
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{tiny}));
            // The purchase price is too small to affect the total tokens
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), ammAlice.tokens()));
            // Bid the tiny amount
            ammAlice.bid(
                alice, IOUAmount{STAmount::cMinValue, STAmount::cMinOffset});
            // Pay slightly higher price
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 0, IOUAmount{tiny * Number{105, -2}}));
            // The purchase price is still too small to affect the total tokens
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), ammAlice.tokens()));
        });
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

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Can't consume whole pool
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(USD(1000000000)),
                    ter(tecPATH_PARTIAL));
                // Overflow
                env(pay(alice, carol, STAmount{USD, UINT64_C(99999999999), -9}),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{USD, UINT64_C(99999999999), -8}),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{xrpIssue(), 99999999}),
                    path(~XRP),
                    sendmax(USD(1000000000)),
                    ter(tecPATH_PARTIAL));
                // Sender doesn't have enough funds
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{xrpIssue(), 99990000}),
                    path(~XRP),
                    sendmax(USD(1000000000)),
                    ter(tecPATH_PARTIAL));
            },
            {{XRP(100), USD(100)}});
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
        // path ~29.9XRP/29.9EUR, ~29.9EUR/~29.99USD. The rest
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
                XRPAmount(10030082730),
                STAmount(EUR, UINT64_C(9970007498125468), -12),
                ammEUR_XRP.tokens()));
            BEAST_EXPECT(ammUSD_EUR.expectBalances(
                STAmount(USD, UINT64_C(9970097277662122), -12),
                STAmount(EUR, UINT64_C(1002999250187452), -11),
                ammUSD_EUR.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{Amounts{
                    XRPAmount(30201749),
                    STAmount(USD, UINT64_C(2990272233787818), -14)}}}));
            // Initial 30,000 + 100
            BEAST_EXPECT(expectLine(env, carol, STAmount{USD, 30100}));
            // Initial 1,000 - 30082730(AMM pool) - 70798251(offer) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1000) - XRPAmount{30082730} - XRPAmount{70798251} -
                    txfee(env, 1)));
        }

        // Default path (with AMM) has a better quality than a non-default path.
        // The max possible liquidity is taken out of default
        // path ~49XRP/49USD. The rest is taken from the offer.
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
                XRPAmount(10050238637),
                STAmount(USD, UINT64_C(995001249687578), -11),
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                2,
                {{Amounts{
                      XRPAmount(50487378),
                      STAmount(EUR, UINT64_C(4998750312422), -11)},
                  Amounts{
                      STAmount(EUR, UINT64_C(4998750312422), -11),
                      STAmount(USD, UINT64_C(4998750312422), -11)}}}));
            // Initial 30,000 + 99.99999999999
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3009999999999999), -11}));
            // Initial 1,000 - 50238637(AMM pool) - 50512622(offer) - 10(tx
            // fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1000) - XRPAmount{50238637} - XRPAmount{50512622} -
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
                // - 100(offer) - 10(tx fee) - one reserve
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30000) - XRP(10000) - XRP(100) - XRP(100) -
                        ammCrtFee(env) - txfee(env, 1)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10000), USD(10100)}});

        // Default path with AMM and Order Book offer.
        // Order Book offer is consumed first.
        // Remaining amount is consumed by AMM.
        {
            Env env(*this);
            fund(env, gw, {alice, bob, carol}, XRP(20000), {USD(2000)});
            env(offer(bob, XRP(50), USD(150)), txflags(tfPassive));
            AMM ammAlice(env, alice, XRP(1000), USD(1050));
            env(pay(alice, carol, USD(200)),
                sendmax(XRP(200)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(1050), USD(1000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(2200)));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

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
                env(rate(gw, 1.25));
                env.close();
                env(offer(carol, EUR(100), GBP(100)));
                env.close();
                // No transfer fee
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(1100), EUR(1000), ammAlice.tokens()));
                // Initial 30,000 - 100(offer) - 25% transfer fee
                BEAST_EXPECT(expectLine(env, carol, GBP(29875)));
                // Initial 30,000 + 100(offer)
                BEAST_EXPECT(expectLine(env, carol, EUR(30100)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{GBP(1000), EUR(1100)}});

        // Payment and transfer fee
        // Scenario:
        // Bob sends 125GBP to pay 100USD to Carol
        // Payment execution:
        // bob's 125GBP/1.25 = 100GBP
        // 100GBP/100EUR AMM offer
        // 100EUR/1 (no AMM tr fee) = 100EUR paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {GBP(200), EUR(200)}, Fund::Acct);
                env(rate(gw, 1.25));
                env.close();
                env(pay(bob, carol, EUR(100)),
                    path(~EUR),
                    sendmax(GBP(125)),
                    txflags(tfPartialPayment));
                env.close();
            },
            {{GBP(1000), EUR(1100)}});

        // Payment and transfer fee, multiple steps
        // Scenario:
        // Dan's offer 200CAN/200GBP
        // AMM 1000GBP/10125EUR
        // Ed's offer 200EUR/200USD
        // Bob sends 195.3125CAN to pay 100USD to Carol
        // Payment execution:
        // bob's 195.3125CAN/1.25 = 156.25CAN -> dan's offer
        // 156.25CAN/156.25GBP 156.25GBP/1.25 = 125GBP -> AMM's offer
        // 125GBP/125EUR 125EUR/1 (no AMM tr fee) = 125EUR -> ed's offer
        // 125EUR/125USD 125USD/1.25 = 100USD paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                Account const ed("ed");
                auto const CAN = gw["CAN"];
                fund(env, gw, {dan}, {CAN(200), GBP(200)}, Fund::Acct);
                fund(env, gw, {ed}, {EUR(200), USD(200)}, Fund::Acct);
                fund(env, gw, {bob}, {CAN(195.3125)}, Fund::Acct);
                env(trust(carol, USD(100)));
                env(rate(gw, 1.25));
                env.close();
                env(offer(dan, CAN(200), GBP(200)));
                env(offer(ed, EUR(200), USD(200)));
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~GBP, ~EUR, ~USD),
                    sendmax(CAN(195.3125)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(expectLine(env, bob, CAN(0)));
                BEAST_EXPECT(expectLine(env, dan, CAN(356.25), GBP(43.75)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(10125), EUR(10000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, ed, EUR(325), USD(75)));
                BEAST_EXPECT(expectLine(env, carol, USD(100)));
            },
            {{GBP(10000), EUR(10125)}});

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(USD(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, STAmount{xrpIssue(), 99999900}),
                    path(~XRP),
                    sendmax(USD(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
            },
            {{XRP(100), USD(100)}});

        // Multiple paths/steps
        {
            Env env(*this);
            auto const ETH = gw["ETH"];
            fund(
                env,
                gw,
                {alice},
                XRP(100000),
                {EUR(50000), BTC(50000), ETH(50000), USD(50000)});
            fund(env, gw, {carol, bob}, XRP(1000), {USD(200)}, Fund::Acct);
            AMM xrp_eur(env, alice, XRP(10100), EUR(10000));
            AMM eur_btc(env, alice, EUR(10000), BTC(10200));
            AMM btc_usd(env, alice, BTC(10100), USD(10000));
            AMM xrp_usd(env, alice, XRP(10150), USD(10200));
            AMM xrp_eth(env, alice, XRP(10000), ETH(10100));
            AMM eth_eur(env, alice, ETH(10900), EUR(11000));
            AMM eur_usd(env, alice, EUR(10100), USD(10000));
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~BTC, ~USD),
                path(~USD),
                path(~ETH, ~EUR, ~USD),
                sendmax(XRP(200)));
            // XRP-ETH-EUR-USD
            // This path provides ~26.06USD/26.2XRP
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10026208900),
                STAmount{ETH, UINT64_C(1007365779244494), -11},
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                STAmount{ETH, UINT64_C(1092634220755506), -11},
                STAmount{EUR, UINT64_C(1097354232078752), -11},
                eth_eur.tokens()));
            BEAST_EXPECT(eur_usd.expectBalances(
                STAmount{EUR, UINT64_C(1012645767921248), -11},
                STAmount{USD, UINT64_C(997393151712086), -11},
                eur_usd.tokens()));

            // XRP-USD path
            // This path provides ~73.9USD/74.1XRP
            BEAST_EXPECT(xrp_usd.expectBalances(
                XRPAmount(10224106246),
                STAmount{USD, UINT64_C(1012606848287914), -11},
                xrp_usd.tokens()));

            // XRP-EUR-BTC-USD
            // This path doesn't provide any liquidity due to how
            // offers are generated in multi-path. Analytical solution
            // shows a different distribution:
            // XRP-EUR-BTC-USD 11.6USD/11.64XRP, XRP-USD 60.7USD/60.8XRP,
            // XRP-ETH-EUR-USD 27.6USD/27.6XRP
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRP(10100), EUR(10000), xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                EUR(10000), BTC(10200), eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                BTC(10100), USD(10000), btc_usd.tokens()));

            BEAST_EXPECT(expectLine(env, carol, USD(300)));
        }

        // Dependent AMM
        {
            Env env(*this);
            auto const ETH = gw["ETH"];
            fund(
                env,
                gw,
                {alice},
                XRP(40000),
                {EUR(50000), BTC(50000), ETH(50000), USD(50000)});
            fund(env, gw, {carol, bob}, XRP(1000), {USD(200)}, Fund::Acct);
            AMM xrp_eur(env, alice, XRP(10100), EUR(10000));
            AMM eur_btc(env, alice, EUR(10000), BTC(10200));
            AMM btc_usd(env, alice, BTC(10100), USD(10000));
            AMM xrp_eth(env, alice, XRP(10000), ETH(10100));
            AMM eth_eur(env, alice, ETH(10900), EUR(11000));
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~BTC, ~USD),
                path(~ETH, ~EUR, ~BTC, ~USD),
                sendmax(XRP(200)));
            // XRP-EUR-BTC-USD path provides ~17.8USD/~18.7XRP
            // XRP-ETH-EUR-BTC-USD path provides ~82.2USD/82.4XRP
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRPAmount(10118738472),
                STAmount{EUR, UINT64_C(9981544436337968), -12},
                xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                STAmount{EUR, UINT64_C(1010116096785173), -11},
                STAmount{BTC, UINT64_C(1009791426968066), -11},
                eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                STAmount{BTC, UINT64_C(1020208573031934), -11},
                USD(9900),
                btc_usd.tokens()));
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10082446396),
                STAmount{ETH, UINT64_C(1001741072778012), -11},
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                STAmount{ETH, UINT64_C(1098258927221988), -11},
                STAmount{EUR, UINT64_C(109172945958103), -10},
                eth_eur.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(300)));
        }

        // AMM offers limit
        // Consuming 30 CLOB offers, results in hitting 30 AMM offers limit.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            fund(env, gw, {bob}, {EUR(400)}, Fund::IOUOnly);
            env(trust(alice, EUR(200)));
            for (int i = 0; i < 30; ++i)
                env(offer(alice, EUR(1.0 + 0.01 * i), XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, EUR(140), XRP(100)));
            env(pay(bob, carol, USD(100)),
                path(~XRP, ~USD),
                sendmax(EUR(400)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            // Carol gets ~29.91USD because of the AMM offers limit
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10030),
                STAmount{USD, UINT64_C(9970089730807577), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3002991026919241), -11}));
            BEAST_EXPECT(expectOffers(env, alice, 1, {{{EUR(140), XRP(100)}}}));
        });
        // This payment is fulfilled
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            fund(env, gw, {bob}, {EUR(400)}, Fund::IOUOnly);
            env(trust(alice, EUR(200)));
            for (int i = 0; i < 29; ++i)
                env(offer(alice, EUR(1.0 + 0.01 * i), XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, EUR(140), XRP(100)));
            env(pay(bob, carol, USD(100)),
                path(~XRP, ~USD),
                sendmax(EUR(400)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10101010102}, USD(9900), ammAlice.tokens()));
            // Carol gets ~100USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3009999999999999), -11}));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{{STAmount{EUR, 391858572, -7}, XRPAmount{27989898}}}}));
        });
    }

    void
    testAMMTokens()
    {
        testcase("AMM Token Pool - AMM with token(s) from another AMM");
        using namespace jtx;

        // AMM with one LPToken from another AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
            AMM ammAMMToken(
                env, alice, EUR(10000), STAmount{ammAlice.lptIssue(), 1000000});
            BEAST_EXPECT(ammAMMToken.expectBalances(
                EUR(10000),
                STAmount(ammAlice.lptIssue(), 1000000),
                ammAMMToken.tokens()));
        });

        // AMM with two LPTokens from other AMMs.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
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
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
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
            fund(env, gw, {alice, carol}, {EUR(10000)}, Fund::IOUOnly);
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
            fund(env, gw, {alice, carol}, {EUR(10000)}, Fund::IOUOnly);
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
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
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
    testRmFundedOffer(FeatureBitset features)
    {
        testcase("Incorrect Removal of Funded Offers");

        // We need at least two paths. One at good quality and one at bad
        // quality.  The bad quality path needs two offer books in a row.
        // Each offer book should have two offers at the same quality, the
        // offers should be completely consumed, and the payment should
        // should require both offers to be satisfied. The first offer must
        // be "taker gets" XRP. Old, broken would remove the first
        // "taker gets" xrp offer, even though the offer is still funded and
        // not used for the payment.

        using namespace jtx;
        Env env{*this, features};

        fund(
            env, gw, {alice, bob, carol}, XRP(10000), {USD(200000), BTC(2000)});

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env(offer(carol, BTC(49), XRP(49)));
        env(offer(carol, BTC(51), XRP(51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env(offer(carol, XRP(50), USD(50)));
        env(offer(carol, XRP(50), USD(50)));

        // Good quality path
        AMM ammCarol(env, carol, BTC(1000), USD(100100));

        PathSet paths(Path(XRP, USD), Path(USD));

        env(pay(alice, bob, USD(100)),
            json(paths.json()),
            sendmax(BTC(1000)),
            txflags(tfPartialPayment));

        BEAST_EXPECT(ammCarol.expectBalances(
            STAmount{BTC, UINT64_C(1001000000374812), -12},
            USD(100000),
            ammCarol.tokens()));

        env.require(balance(bob, USD(200100)));
        BEAST_EXPECT(isOffer(env, carol, BTC(49), XRP(49)));
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

            env.fund(XRP(20000), alice, bob, carol, gw1, gw2);
            env.fund(XRP(20000), dan);
            env.trust(USD1(20000), alice, bob, carol, dan);
            env.trust(USD2(1000), alice, bob, carol, dan);

            env(pay(gw1, dan, USD1(10050)));
            env(pay(gw1, bob, USD1(50)));
            env(pay(gw2, bob, USD2(50)));

            AMM ammDan(env, dan, XRP(10000), USD1(10050));

            env(pay(alice, carol, USD2(50)),
                path(~USD1, bob),
                sendmax(XRP(50)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(ammDan.expectBalances(
                XRP(10050), USD1(10000), ammDan.tokens()));

            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, XRP(20000) - XRP(50) - txfee(env, 1)));
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
    testOfferCrossWithLimitOverride(FeatureBitset features)
    {
        testcase("Offer Crossing with Limit Override");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(200000), gw, alice, bob);

        env(trust(alice, USD(1000)));

        env(pay(gw, alice, alice["USD"](500)));

        AMM ammAlice(env, alice, XRP(150000), USD(51));
        env(offer(bob, USD(1), XRP(3000)));

        BEAST_EXPECT(
            ammAlice.expectBalances(XRP(153000), USD(50), ammAlice.tokens()));

        auto jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(200000) - XRP(3000) - env.current()->fees().base * 1)
                          .xrp()));
    }

    void
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, XRP(10000));
        env.require(owners(bob, 0));

        env(trust(alice, USD(100)));
        env(trust(bob, USD(1000)));
        env(pay(gw, bob, USD(1000)));

        env.require(owners(alice, 1), owners(bob, 1));

        env(pay(gw, alice, alice["USD"](100)));
        AMM ammBob(env, bob, USD(200), XRP(1500));

        env(pay(alice, alice, XRP(500)), sendmax(USD(100)));

        BEAST_EXPECT(
            ammBob.expectBalances(USD(300), XRP(1000), ammBob.tokens()));
        BEAST_EXPECT(expectLine(env, alice, USD(0)));

        auto jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10000) + XRP(500) - env.current()->fees().base * 2)
                          .xrp()));
    }

    void
    testCurrencyConversionInParts(FeatureBitset features)
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
                        ammCrtFee(env) - txfee(env, 2)));
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
    testOfferFeesConsumeFunds(FeatureBitset features)
    {
        testcase("Offer Fees Consume Funds");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const gw3 = Account{"gateway_3"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];
        auto const USD3 = gw3["USD"];

        // Provide micro amounts to compensate for fees to make results round
        // nice.
        // reserve: Alice has 3 entries in the ledger, via trust lines
        // fees:
        //  1 for each trust limit == 3 (alice < mtgox/amazon/bitstamp) +
        //  1 for payment          == 4
        auto const starting_xrp = XRP(100) +
            env.current()->fees().accountReserve(3) +
            env.current()->fees().base * 4;

        env.fund(starting_xrp, gw1, gw2, gw3, alice);
        env.fund(XRP(2000), bob);

        env(trust(alice, USD1(1000)));
        env(trust(alice, USD2(1000)));
        env(trust(alice, USD3(1000)));
        env(trust(bob, USD1(1200)));
        env(trust(bob, USD2(1100)));

        env(pay(gw1, bob, bob["USD"](1200)));

        AMM ammBob(env, bob, XRP(1000), USD1(1200));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice, USD1(200), XRP(200)));

        // The pool gets only 100XRP for ~109.09USD, even though
        // it can exchange more.
        BEAST_EXPECT(ammBob.expectBalances(
            XRP(1100),
            STAmount{USD1, UINT64_C(1090909090909091), -12},
            ammBob.tokens()));

        auto jrr = ledgerEntryState(env, alice, gw1, "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] ==
            "109.090909090909");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] == XRP(350).value().getText());
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, XRP(200000));

        env(rate(gw, 1.005));

        env(trust(alice, USD(1000)));
        env(trust(bob, USD(1000)));

        env(pay(gw, bob, USD(1)));
        env(pay(gw, alice, USD(200)));

        AMM ammAlice(env, alice, USD(150), XRP(150100));
        env(offer(bob, XRP(100), USD(0.1)));

        BEAST_EXPECT(ammAlice.expectBalances(
            USD(150.1), XRP(150000), ammAlice.tokens()));

        auto const jrr = ledgerEntryState(env, bob, gw, "USD");
        // Bob pays 0.005 transfer fee. Note 10**-10 round-off.
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] == "-0.8995000001");
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

        env(pay(gw, bob, bob["USD"](2200)));

        AMM ammBob(env, bob, XRP(1000), USD(2200));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env(offer(alice, USD(100), XRP(100)), json(jss::Flags, tfSell));
        BEAST_EXPECT(
            ammBob.expectBalances(XRP(1100), USD(2000), ammBob.tokens()));
        BEAST_EXPECT(expectLine(env, alice, USD(200)));
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
                STAmount{USD, UINT64_C(1978239366963403), -13},
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
            // all of the offer is consumed. The above is behaviour
            // in the original test. AMM can generate "unlimited" (subject
            // to overflow) taker pays as taker gets approaches to the
            // entire pool amount. The test therefore succeeds.
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(10000)}, Fund::All);
            AMM ammBob(env, bob, XRP(500), USD(5));

            env(offer(alice, USD(1), XRP(501), tfSell | tfFillOrKill),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(1001),
                STAmount{USD, UINT64_C(2497502497502498), -15},
                ammBob.tokens()));
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

                // AMM doesn't pay the transfer fee
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

            // AMM doesn't pay the transfer fee
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

            // AMM doesn't pay the transfer fee
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

            // AMM doesn't pay the transfer fee
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

            // AMM pay doesn't transfer fee
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
    testDirectToDirectPath(FeatureBitset features)
    {
        // The offer crossing code expects that a DirectStep is always
        // preceded by a BookStep.  In one instance the default path
        // was not matching that assumption.  Here we recreate that case
        // so we can prove the bug stays fixed.
        testcase("Direct to Direct path");

        using namespace jtx;

        Env env{*this, features};

        auto const ann = Account("ann");
        auto const bob = Account("bob");
        auto const cam = Account("cam");
        auto const carol = Account("carol");
        auto const A_BUX = ann["BUX"];
        auto const B_BUX = bob["BUX"];

        auto const fee = env.current()->fees().base;
        env.fund(XRP(1000), carol);
        env.fund(reserve(env, 4) + (fee * 5), ann, bob, cam);
        env.close();

        env(trust(ann, B_BUX(40)));
        env(trust(cam, A_BUX(40)));
        env(trust(bob, A_BUX(30)));
        env(trust(cam, B_BUX(40)));
        env(trust(carol, B_BUX(400)));
        env(trust(carol, A_BUX(400)));
        env.close();

        env(pay(ann, cam, A_BUX(35)));
        env(pay(bob, cam, B_BUX(35)));
        env(pay(bob, carol, B_BUX(400)));
        env(pay(ann, carol, A_BUX(400)));

        AMM ammCarol(env, carol, A_BUX(300), B_BUX(330));

        // cam puts an offer on the books that her upcoming offer could cross.
        // But this offer should be deleted, not crossed, by her upcoming
        // offer.
        env(offer(cam, A_BUX(29), B_BUX(30), tfPassive));
        env.close();
        env.require(balance(cam, A_BUX(35)));
        env.require(balance(cam, B_BUX(35)));
        env.require(offers(cam, 1));

        // This offer caused the assert.
        env(offer(cam, B_BUX(30), A_BUX(30)));

        // AMM is consumed up to the first cam Offer quality
        BEAST_EXPECT(ammCarol.expectBalances(
            STAmount{A_BUX, UINT64_C(3093541659651603), -13},
            STAmount{B_BUX, UINT64_C(3200215509984419), -13},
            ammCarol.tokens()));
        BEAST_EXPECT(expectOffers(
            env,
            cam,
            1,
            {{Amounts{
                STAmount{B_BUX, UINT64_C(200215509984419), -13},
                STAmount{A_BUX, UINT64_C(200215509984419), -13}}}}));
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
                env, alice, USD(1000), XRP(1000), ter(tecAMM_UNFUNDED));
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
    testFlags()
    {
        testcase("Flags");
        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            auto const info = env.rpc(
                "json",
                "account_info",
                std::string(
                    "{\"account\": \"" + to_string(ammAlice.ammAccount()) +
                    "\"}"));
            auto const flags =
                info[jss::result][jss::account_data][jss::Flags].asUInt();
            BEAST_EXPECT(
                flags &
                (lsfAMM | lsfDisableMaster | lsfDefaultRipple |
                 lsfDepositAuth));
        });
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
        testRmFundedOffer(all);
        testEnforceNoRipple(all);
        testFillModes(all);
        testOfferCrossWithXRP(all);
        testOfferCrossWithLimitOverride(all);
        testCurrencyConversionEntire(all);
        testCurrencyConversionInParts(all);
        testCrossCurrencyStartXRP(all);
        testCrossCurrencyEndXRP(all);
        testCrossCurrencyBridged(all);
        testOfferFeesConsumeFunds(all);
        testOfferCreateThenCross(all);
        testSellFlagExceedLimit(all);
        testGatewayCrossCurrency(all);
        // testPartialCross
        // testXRPDirectCross
        // testDirectCross
        testBridgedCross(all);
        // testSellOffer
        testSellWithFillOrKill(all);
        testTransferRateOffer(all);
        testSelfIssueOffer(all);
        testBadPathAssert(all);
        testSellFlagBasic(all);
        testDirectToDirectPath(all);
        // testSelfCrossLowQualityOffer
        // testOfferInScaling
        // testOfferInScalingWithXferRate
        // testOfferThresholdWithReducedFunds
        // testTinyOffer
        // testSelfPayXferFeeOffer
        // testSelfPayXferFeeOffer
        testRequireAuth(all);
        testMissingAuth(all);
        // testRCSmoketest
    }

    void
    path_find_consume_all()
    {
        testcase("path find consume all");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(100000250), alice);
        fund(env, gw, {carol, bob}, {USD(100)}, Fund::All);
        fund(env, gw, {alice}, {USD(100)}, Fund::IOUOnly);
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
        // Alice sends all requested 100,000,000XRP
        BEAST_EXPECT(sa == XRP(100000000));
        // Bob gets ~99.99USD. This is the amount Bob
        // can get out of AMM for 100,000,000XRP.
        BEAST_EXPECT(equal(
            da, STAmount{bob["USD"].issue(), UINT64_C(999999000001), -10}));
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
        env.fund(XRP(10000), alice, bob, carol, gw);
        env(rate(gw, 1.1));
        env.trust(AUD(2000), bob, carol);
        env(pay(gw, carol, AUD(50)));
        env.close();
        AMM ammCarol(env, carol, XRP(40), AUD(50));
        env(pay(alice, bob, AUD(10)), sendmax(XRP(100)), paths(XRP));
        env.close();
        BEAST_EXPECT(
            ammCarol.expectBalances(XRP(50), AUD(40), ammCarol.tokens()));
        BEAST_EXPECT(expectLine(env, bob, AUD(10)));

        auto const result =
            find_paths(env, alice, bob, Account(bob)["USD"](25));
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
        testcase("Path Find: non-XRP -> non-XRP, same currency");
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
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        Env env(*this, features);

        env.fund(XRP(10000), alice, gw);
        // This removes no ripple for carol,
        // different from the original test
        fund(env, gw, {carol}, XRP(10000), {}, Fund::Acct);
        auto const AMMXRPPool = env.current()->fees().increment * 2;
        env.fund(reserve(env, 5) + ammCrtFee(env) + AMMXRPPool, bob);
        env.trust(USD(1000), alice, bob, carol);
        env.trust(EUR(1000), alice, bob, carol);

        env(pay(gw, alice, EUR(50)));
        env(pay(gw, bob, USD(150)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/USD offer will require two
        // recursive calls to the EUR/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry. This
        // is the second error case to test (when flowV1 is used).
        env(offer(bob, EUR(50), XRP(50)));
        AMM ammBob(env, bob, AMMXRPPool, USD(150));

        env(pay(alice, carol, USD(1000000)),
            path(~XRP, ~USD),
            sendmax(EUR(500)),
            txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolUSD = env.balance(carol, USD).value();
        BEAST_EXPECT(carolUSD > USD(0) && carolUSD < USD(50));
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book Step");

        using namespace jtx;

        {
            // simple IOU/IOU offer
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(10000),
                {BTC(100), USD(150)},
                Fund::All);

            AMM ammBob(env, bob, BTC(100), USD(150));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(BTC(50)));

            BEAST_EXPECT(expectLine(env, alice, BTC(50)));
            BEAST_EXPECT(expectLine(env, bob, BTC(0)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, USD(200)));
            BEAST_EXPECT(
                ammBob.expectBalances(BTC(150), USD(100), ammBob.tokens()));
        }
        {
            // simple IOU/XRP XRP/IOU offer
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(10000),
                {BTC(100), USD(150)},
                Fund::All);

            AMM ammBobBTC_XRP(env, bob, BTC(100), XRP(150));
            AMM ammBobXRP_USD(env, bob, XRP(100), USD(150));

            env(pay(alice, carol, USD(50)), path(~XRP, ~USD), sendmax(BTC(50)));

            BEAST_EXPECT(expectLine(env, alice, BTC(50)));
            BEAST_EXPECT(expectLine(env, bob, BTC(0)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, USD(200)));
            BEAST_EXPECT(ammBobBTC_XRP.expectBalances(
                BTC(150), XRP(100), ammBobBTC_XRP.tokens()));
            BEAST_EXPECT(ammBobXRP_USD.expectBalances(
                XRP(150), USD(100), ammBobXRP_USD.tokens()));
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(10000),
                {USD(150)},
                Fund::All);

            AMM ammBob(env, bob, XRP(100), USD(150));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(XRP(50)));

            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10000 - 50)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(10000) - XRP(100) - ammCrtFee(env)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, USD(200)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(150), USD(100), ammBob.tokens()));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(10000),
                {USD(100)},
                Fund::All);

            AMM ammBob(env, bob, USD(100), XRP(150));

            env(pay(alice, carol, XRP(50)), path(~XRP), sendmax(USD(50)));

            BEAST_EXPECT(expectLine(env, alice, USD(50)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(10000) - XRP(150) - ammCrtFee(env)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, carol, XRP(10000 + 50)));
            BEAST_EXPECT(
                ammBob.expectBalances(USD(150), XRP(100), ammBob.tokens()));
        }
        {
            // test unfunded offers are removed when payment succeeds
            Env env(*this, features);

            env.fund(XRP(10000), alice, carol, gw);
            env.fund(XRP(10000), bob);
            env.trust(USD(1000), alice, bob, carol);
            env.trust(BTC(1000), alice, bob, carol);
            env.trust(EUR(1000), alice, bob, carol);

            env(pay(gw, alice, BTC(60)));
            env(pay(gw, bob, USD(200)));
            env(pay(gw, bob, EUR(150)));

            env(offer(bob, BTC(50), USD(50)));
            env(offer(bob, BTC(40), EUR(50)));
            AMM ammBob(env, bob, EUR(100), USD(150));

            // unfund offer
            env(pay(bob, gw, EUR(50)));
            BEAST_EXPECT(isOffer(env, bob, BTC(50), USD(50)));
            BEAST_EXPECT(isOffer(env, bob, BTC(40), EUR(50)));

            env(pay(alice, carol, USD(50)),
                path(~USD),
                path(~EUR, ~USD),
                sendmax(BTC(60)));

            env.require(balance(alice, BTC(10)));
            env.require(balance(bob, BTC(50)));
            env.require(balance(bob, USD(0)));
            env.require(balance(bob, EUR(0)));
            env.require(balance(carol, USD(50)));
            // used in the payment
            BEAST_EXPECT(!isOffer(env, bob, BTC(50), USD(50)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(40), EUR(50)));
            // unchanged
            BEAST_EXPECT(
                ammBob.expectBalances(EUR(100), USD(150), ammBob.tokens()));
        }
        {
            // test unfunded offers are returned when the payment fails.
            // bob makes two offers: a funded 50 USD for 50 BTC and an unfunded
            // 50 EUR for 60 BTC. alice pays carol 61 USD with 61 BTC. alice
            // only has 60 BTC, so the payment will fail. The payment uses two
            // paths: one through bob's funded offer and one through his
            // unfunded offer. When the payment fails `flow` should return the
            // unfunded offer. This test is intentionally similar to the one
            // that removes unfunded offers when the payment succeeds.
            Env env(*this, features);

            env.fund(XRP(10000), bob, carol, gw);
            // Sets rippling on, this is different from
            // the original test
            fund(env, gw, {alice}, XRP(10000), {}, Fund::Acct);
            env.trust(USD(1000), alice, bob, carol);
            env.trust(BTC(1000), alice, bob, carol);
            env.trust(EUR(1000), alice, bob, carol);

            env(pay(gw, alice, BTC(60)));
            env(pay(gw, bob, BTC(100)));
            env(pay(gw, bob, USD(100)));
            env(pay(gw, bob, EUR(50)));
            env(pay(gw, carol, EUR(1)));

            // This is multiplath, which generates limited # of offers
            AMM ammBobBTC_USD(env, bob, BTC(50), USD(50));
            env(offer(bob, BTC(60), EUR(50)));
            env(offer(carol, BTC(1000), EUR(1)));
            env(offer(bob, EUR(50), USD(50)));

            // unfund offer
            env(pay(bob, gw, EUR(50)));
            BEAST_EXPECT(ammBobBTC_USD.expectBalances(
                BTC(50), USD(50), ammBobBTC_USD.tokens()));
            BEAST_EXPECT(isOffer(env, bob, BTC(60), EUR(50)));
            BEAST_EXPECT(isOffer(env, carol, BTC(1000), EUR(1)));
            BEAST_EXPECT(isOffer(env, bob, EUR(50), USD(50)));

            auto flowJournal = env.app().logs().journal("Flow");
            auto const flowResult = [&] {
                STAmount deliver(USD(51));
                STAmount smax(BTC(61));
                PaymentSandbox sb(env.current().get(), tapNONE);
                STPathSet paths;
                auto IPE = [](Issue const& iss) {
                    return STPathElement(
                        STPathElement::typeCurrency | STPathElement::typeIssuer,
                        xrpAccount(),
                        iss.currency,
                        iss.account);
                };
                {
                    // BTC -> USD
                    STPath p1({IPE(USD.issue())});
                    paths.push_back(p1);
                    // BTC -> EUR -> USD
                    STPath p2({IPE(EUR.issue()), IPE(USD.issue())});
                    paths.push_back(p2);
                }

                return flow(
                    sb,
                    deliver,
                    alice,
                    carol,
                    paths,
                    false,
                    false,
                    true,
                    false,
                    std::nullopt,
                    smax,
                    flowJournal);
            }();

            BEAST_EXPECT(flowResult.removableOffers.size() == 1);
            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    if (flowResult.removableOffers.empty())
                        return false;
                    Sandbox sb(&view, tapNONE);
                    for (auto const& o : flowResult.removableOffers)
                        if (auto ok = sb.peek(keylet::offer(o)))
                            offerDelete(sb, ok, flowJournal);
                    sb.apply(view);
                    return true;
                });

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(ammBobBTC_USD.expectBalances(
                BTC(50), USD(50), ammBobBTC_USD.tokens()));
            BEAST_EXPECT(isOffer(env, carol, BTC(1000), EUR(1)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(60), EUR(50)));
        }
        {
            // Do not produce more in the forward pass than the reverse pass
            // This test uses a path that whose reverse pass will compute a
            // 0.5 USD input required for a 1 EUR output. It sets a sendmax of
            // 0.4 USD, so the payment engine will need to do a forward pass.
            // Without limits, the 0.4 USD would produce 1000 EUR in the forward
            // pass. This test checks that the payment produces 1 EUR, as
            // expected.

            Env env(*this, features);
            env.fund(XRP(10000), bob, carol, gw);
            fund(env, gw, {alice}, XRP(10000), {}, Fund::Acct);
            env.trust(USD(1000), alice, bob, carol);
            env.trust(EUR(1000), alice, bob, carol);

            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, EUR(1000)));
            env(pay(gw, bob, USD(1000)));

            // env(offer(bob, USD(1), drops(2)), txflags(tfPassive));
            AMM ammBob(env, bob, USD(8), XRPAmount{21});
            env(offer(bob, drops(1), EUR(1000)), txflags(tfPassive));

            env(pay(alice, carol, EUR(1)),
                path(~XRP, ~EUR),
                sendmax(USD(0.4)),
                txflags(tfNoRippleDirect | tfPartialPayment));

            BEAST_EXPECT(expectLine(env, carol, EUR(1)));
            BEAST_EXPECT(ammBob.expectBalances(
                USD(8.4), XRPAmount{20}, ammBob.tokens()));
        }
    }

    void
    testTransferRate(FeatureBitset features)
    {
        testcase("Transfer Rate");

        using namespace jtx;

        {
            // transfer fee on AMM
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10000), {USD(1000)});
            env(rate(gw, 1.25));
            env.close();

            AMM ammBob(env, bob, XRP(100), USD(150));
            // bob is charged the transfer fee on AMM create
            // 150*0.25 = 37.5
            BEAST_EXPECT(expectLine(env, bob, USD(1000 - 150 - 150 * 0.25)));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(XRP(50)));
            env.close();

            // no other charge
            BEAST_EXPECT(expectLine(env, bob, USD(1000 - 150 - 150 * 0.25)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(150), USD(100), ammBob.tokens()));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10000 - 50)));
            BEAST_EXPECT(expectLine(env, carol, USD(1050)));
        }

        {
            // Transfer fee AMM and offer
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(10000),
                {USD(1000), EUR(1000)});
            env(rate(gw, 1.25));
            env.close();

            AMM ammBob(env, bob, XRP(100), USD(140));
            // bob is charged the transfer fee on AMM create
            // 140*0.25 = 35
            BEAST_EXPECT(expectLine(env, bob, USD(1000 - 140 - 140 * 0.25)));

            env(offer(bob, USD(50), EUR(50)));

            env(pay(alice, carol, EUR(40)), path(~USD, ~EUR), sendmax(XRP(40)));

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(140), USD(100), ammBob.tokens()));
            // bob is charged 25% on the takerGets USD/EUR offer
            // 40*0.25 = 10
            BEAST_EXPECT(expectLine(env, bob, EUR(1000 - 40 - 40 * 0.25)));
            // bob got 40USD back from the offer
            BEAST_EXPECT(
                expectLine(env, bob, USD(1000 - 140 - 140 * 0.25 + 40)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10000 - 40)));
            BEAST_EXPECT(expectLine(env, carol, EUR(1040)));
        }

        {
            // Transfer fee tow consecutive AMM
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(10000),
                {USD(1000), EUR(1000)});
            env(rate(gw, 1.25));
            env.close();

            AMM ammBobXRP_USD(env, bob, XRP(100), USD(140));
            // bob is charged the transfer fee on AMM create
            // 140*0.25 = 35
            BEAST_EXPECT(expectLine(env, bob, USD(1000 - 140 - 140 * 0.25)));

            AMM ammBobUSD_EUR(env, bob, USD(100), EUR(140));
            // bob is charged the transfer fee on AMM create
            // 140*0.25 = 35
            BEAST_EXPECT(expectLine(env, bob, EUR(1000 - 140 - 140 * 0.25)));
            // bob is charged the transfer fee on AMM create
            // 100*0.25 = 25
            BEAST_EXPECT(expectLine(
                env, bob, USD(1000 - 140 - 100 - (140 + 100) * 0.25)));

            env(pay(alice, carol, EUR(40)), path(~USD, ~EUR), sendmax(XRP(40)));

            BEAST_EXPECT(ammBobXRP_USD.expectBalances(
                XRP(140), USD(100), ammBobXRP_USD.tokens()));
            BEAST_EXPECT(ammBobUSD_EUR.expectBalances(
                USD(140), EUR(100), ammBobUSD_EUR.tokens()));
            // no other charges on bob
            BEAST_EXPECT(expectLine(
                env, bob, USD(1000 - 140 - 100 - (140 + 100) * 0.25)));
            BEAST_EXPECT(expectLine(env, bob, EUR(1000 - 140 - 140 * 0.25)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10000 - 40)));
            BEAST_EXPECT(expectLine(env, carol, EUR(1040)));
        }

        {
            // First pass through a strand redeems, second pass issues, through
            // an offer limiting step is not an endpoint
            Env env(*this, features);
            auto const USDA = alice["USD"];
            auto const USDB = bob["USD"];
            Account const dan("dan");

            env.fund(XRP(10000), bob, carol, dan, gw);
            fund(env, {alice}, XRP(10000));
            env(rate(gw, 1.25));
            env.trust(USD(2000), alice, bob, carol, dan);
            env.trust(EUR(2000), carol, dan);
            env.trust(USDA(1000), bob);
            env.trust(USDB(1000), gw);
            env(pay(gw, bob, USD(50)));
            // Includes the transfer fee on AMM crete
            env(pay(gw, dan, EUR(1312.5)));
            // Includes the transfer fee on AMM crete
            env(pay(gw, dan, USD(1250)));
            AMM ammDan(env, dan, USD(1000), EUR(1050));

            // alice -> bob -> gw -> carol. $50 should have transfer fee; $10,
            // no fee
            env(pay(alice, carol, EUR(50)),
                path(bob, gw, ~EUR),
                sendmax(USDA(60)),
                txflags(tfNoRippleDirect));

            BEAST_EXPECT(
                ammDan.expectBalances(USD(1050), EUR(1000), ammDan.tokens()));
            // Dan is charged the transfer fee on AMM create
            // 1000*0.25
            BEAST_EXPECT(expectLine(env, dan, USD(0)));
            // Dan is charged the transfer fee on AMM create
            // 1050*0.25
            BEAST_EXPECT(expectLine(env, dan, EUR(0)));
            BEAST_EXPECT(expectLine(env, bob, USD(-10)));
            BEAST_EXPECT(expectLine(env, bob, USDA(60)));
            BEAST_EXPECT(expectLine(env, carol, EUR(50)));
        }
    }

    void
    testLimitQuality()
    {
        // Single path with two offers and limit quality. The quality limit is
        // such that the first offer should be taken but the second should not.
        // The total amount delivered should be the sum of the two offers and
        // sendMax should be more than the first offer.
        testcase("limitQuality");
        using namespace jtx;

        {
            Env env(*this);

            fund(env, gw, {alice, bob, carol}, XRP(10000), {USD(2000)});

            AMM ammBob(env, bob, XRP(1000), USD(1050));
            env(offer(bob, XRP(100), USD(50)));

            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(1050), USD(1000), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(2050)));
            BEAST_EXPECT(expectOffers(env, bob, 1, {{{XRP(100), USD(50)}}}));
        }
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;

        for (auto const withFix : {true, false})
        {
            auto const feats = [&withFix]() -> FeatureBitset {
                if (withFix)
                    return supported_amendments();
                return supported_amendments() - FeatureBitset{fix1781};
            }();
            {
                // Payment path starting with XRP
                Env env(*this, feats);
                // Note, if alice doesn't have default ripple, then pay fails
                // with tecPATH_DRY.
                fund(
                    env,
                    gw,
                    {alice, bob},
                    XRP(10000),
                    {USD(200), EUR(200)},
                    Fund::All);

                AMM ammAliceXRP_USD(env, alice, XRP(100), USD(101));
                AMM ammAliceXRP_EUR(env, alice, XRP(100), EUR(101));
                env.close();

                TER const expectedTer =
                    withFix ? TER{temBAD_PATH_LOOP} : TER{tesSUCCESS};
                env(pay(alice, bob, EUR(1)),
                    path(~USD, ~XRP, ~EUR),
                    sendmax(XRP(1)),
                    txflags(tfNoRippleDirect),
                    ter(expectedTer));
            }
            pass();
        }
        {
            // Payment path ending with XRP
            Env env(*this);
            // Note, if alice doesn't have default ripple, then pay fails
            // with tecPATH_DRY.
            fund(
                env,
                gw,
                {alice, bob},
                XRP(10000),
                {USD(200), EUR(200)},
                Fund::All);

            AMM ammAliceXRP_USD(env, alice, XRP(100), USD(100));
            AMM ammAliceXRP_EUR(env, alice, XRP(100), EUR(100));
            // EUR -> //XRP -> //USD ->XRP
            env(pay(alice, bob, XRP(1)),
                path(~XRP, ~USD, ~XRP),
                sendmax(EUR(1)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
        {
            // Payment where loop is formed in the middle of the path, not on an
            // endpoint
            auto const JPY = gw["JPY"];
            Env env(*this);
            // Note, if alice doesn't have default ripple, then pay fails
            // with tecPATH_DRY.
            fund(
                env,
                gw,
                {alice, bob},
                XRP(10000),
                {USD(200), EUR(200), JPY(200)},
                Fund::All);

            AMM ammAliceXRP_USD(env, alice, XRP(100), USD(100));
            AMM ammAliceXRP_EUR(env, alice, XRP(100), EUR(100));
            AMM ammAliceXRP_JPY(env, alice, XRP(100), JPY(100));

            env(pay(alice, bob, JPY(1)),
                path(~XRP, ~EUR, ~XRP, ~JPY),
                sendmax(USD(1)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
    }

    void
    testStepLimit(FeatureBitset features)
    {
        testcase("Step Limit");

        using namespace jtx;
        Env env(*this, features);
        auto const dan = Account("dan");
        auto const ed = Account("ed");

        fund(env, gw, {ed}, XRP(100000000), {USD(11)});
        env.fund(XRP(100000000), alice, bob, carol, dan);
        env.trust(USD(1), bob);
        env(pay(gw, bob, USD(1)));
        env.trust(USD(1), dan);
        env(pay(gw, dan, USD(1)));
        n_offers(env, 2000, bob, XRP(1), USD(1));
        n_offers(env, 1, dan, XRP(1), USD(1));
        AMM ammEd(env, ed, XRP(9), USD(11));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes Bob's first
        // offer, removes 999 more as unfunded, then hits the step limit.
        env(offer(alice, USD(1000), XRP(1000)));
        env.require(
            balance(alice, STAmount{USD, UINT64_C(2050126257867561), -15}));
        env.require(owners(alice, 2));
        env.require(balance(bob, USD(0)));
        env.require(owners(bob, 1001));
        env.require(balance(dan, USD(1)));
        env.require(owners(dan, 2));

        // Carol offers to buy 1000 XRP for 1000 USD. She removes Bob's next
        // 1000 offers as unfunded and hits the step limit.
        env(offer(carol, USD(1000), XRP(1000)));
        env.require(balance(carol, USD(none)));
        env.require(owners(carol, 1));
        env.require(balance(bob, USD(0)));
        env.require(owners(bob, 1));
        env.require(balance(dan, USD(1)));
        env.require(owners(dan, 2));
    }

    void
    test_convert_all_of_an_asset(FeatureBitset features)
    {
        testcase("Convert all of an asset using DeliverMin");

        using namespace jtx;
        auto const dan = Account("dan");

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol}, XRP(10000));
            env.trust(USD(100), alice, bob, carol);
            env(pay(alice, bob, USD(10)),
                delivermin(USD(10)),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(USD(-5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(XRP(5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(Account(carol)["USD"](5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(USD(15)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(gw, carol, USD(50)));
            AMM ammCarol(env, carol, XRP(10), USD(15));
            env(pay(alice, bob, USD(10)),
                paths(XRP),
                delivermin(USD(7)),
                txflags(tfPartialPayment),
                sendmax(XRP(5)),
                ter(tecPATH_PARTIAL));
            env.require(balance(alice, XRP(9999.99999)));
            env.require(balance(bob, XRP(10000)));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob}, XRP(10000));
            env.trust(USD(1100), alice, bob);
            env(pay(gw, bob, USD(1100)));
            AMM ammBob(env, bob, XRP(1000), USD(1100));
            env(pay(alice, alice, USD(10000)),
                paths(XRP),
                delivermin(USD(100)),
                txflags(tfPartialPayment),
                sendmax(XRP(100)));
            env.require(balance(alice, USD(100)));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol}, XRP(10000));
            env.trust(USD(1200), bob, carol);
            env(pay(gw, bob, USD(1200)));
            AMM ammBob(env, bob, XRP(5500), USD(1200));
            env(pay(alice, carol, USD(10000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1000)),
                ter(tecPATH_PARTIAL));
            env(pay(alice, carol, USD(10000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1100)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(6600), USD(1000), ammBob.tokens()));
            env.require(balance(carol, USD(200)));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol, dan}, XRP(10000));
            env.trust(USD(1100), bob, carol, dan);
            env(pay(gw, bob, USD(100)));
            env(pay(gw, dan, USD(1100)));
            env(offer(bob, XRP(100), USD(100)));
            env(offer(bob, XRP(1000), USD(100)));
            AMM ammDan(env, dan, XRP(1000), USD(1100));
            env(pay(alice, carol, USD(10000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(200)));
            env.require(balance(bob, USD(0)));
            env.require(balance(carol, USD(200)));
            BEAST_EXPECT(
                ammDan.expectBalances(XRP(1100), USD(1000), ammDan.tokens()));
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace jtx;
        Account const becky{"becky"};

        bool const supportsPreauth = {features[featureDepositPreauth]};

        // The initial implementation of DepositAuth had a bug where an
        // account with the DepositAuth flag set could not make a payment
        // to itself.  That bug was fixed in the DepositPreauth amendment.
        Env env(*this, features);
        fund(env, gw, {alice, becky}, XRP(5000));
        env.close();

        env.trust(USD(1000), alice);
        env.trust(USD(1000), becky);
        env.close();

        env(pay(gw, alice, USD(500)));
        env.close();

        AMM ammAlice(env, alice, XRP(100), USD(140));

        // becky pays herself USD (10) by consuming part of alice's offer.
        // Make sure the payment works if PaymentAuth is not involved.
        env(pay(becky, becky, USD(10)), path(~USD), sendmax(XRP(10)));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount(107692308), USD(130), ammAlice.tokens()));

        // becky decides to require authorization for deposits.
        env(fset(becky, asfDepositAuth));
        env.close();

        // becky pays herself again.  Whether it succeeds depends on
        // whether featureDepositPreauth is enabled.
        TER const expect{
            supportsPreauth ? TER{tesSUCCESS} : TER{tecNO_PERMISSION}};

        env(pay(becky, becky, USD(10)),
            path(~USD),
            sendmax(XRP(10)),
            ter(expect));

        env.close();
    }

    void
    testPayIOU()
    {
        // Exercise IOU payments and non-direct XRP payments to an account
        // that has the lsfDepositAuth flag set.
        testcase("Pay IOU");

        using namespace jtx;

        Env env(*this);

        fund(env, gw, {alice, bob, carol}, XRP(10000));
        env.trust(USD(1000), alice, bob, carol);
        env.close();

        env(pay(gw, alice, USD(150)));
        env(pay(gw, carol, USD(150)));
        AMM ammCarol(env, carol, USD(100), XRPAmount(101));

        // Make sure bob's trust line is all set up so he can receive USD.
        env(pay(alice, bob, USD(50)));
        env.close();

        // bob sets the lsfDepositAuth flag.
        env(fset(bob, asfDepositAuth), require(flags(bob, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedIouPayments = [this, &env]() {
            env.require(flags(bob, asfDepositAuth));

            // Capture bob's balances before hand to confirm they don't change.
            PrettyAmount const bobXrpBalance{env.balance(bob, XRP)};
            PrettyAmount const bobUsdBalance{env.balance(bob, USD)};

            env(pay(alice, bob, USD(50)), ter(tecNO_PERMISSION));
            env.close();

            // Note that even though alice is paying bob in XRP, the payment
            // is still not allowed since the payment passes through an offer.
            env(pay(alice, bob, drops(1)),
                sendmax(USD(1)),
                ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(bobXrpBalance == env.balance(bob, XRP));
            BEAST_EXPECT(bobUsdBalance == env.balance(bob, USD));
        };

        //  Test when bob has an XRP balance > base reserve.
        failedIouPayments();

        // Set bob's XRP balance == base reserve.  Also demonstrate that
        // bob can make payments while his lsfDepositAuth flag is set.
        env(pay(bob, alice, USD(25)));
        env.close();

        {
            STAmount const bobPaysXRP{env.balance(bob, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob, alice, bobPaysXRP), fee(bobPaysFee));
            env.close();
        }

        // Test when bob's XRP balance == base reserve.
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0));
        BEAST_EXPECT(env.balance(bob, USD) == USD(25));
        failedIouPayments();

        // Test when bob has an XRP balance == 0.
        env(noop(bob), fee(reserve(env, 0)));
        env.close();

        BEAST_EXPECT(env.balance(bob, XRP) == XRP(0));
        failedIouPayments();

        // Give bob enough XRP for the fee to clear the lsfDepositAuth flag.
        env(pay(alice, bob, drops(env.current()->fees().base)));

        // bob clears the lsfDepositAuth and the next payment succeeds.
        env(fclear(bob, asfDepositAuth));
        env.close();

        env(pay(alice, bob, USD(50)));
        env.close();

        env(pay(alice, bob, drops(1)), sendmax(USD(1)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(
            USD(101), XRPAmount(100), ammCarol.tokens()));
    }

    void
    testRippleState(FeatureBitset features)
    {
        testcase("RippleState Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account alice{"alice"};
        Account bob{"bob"};

        env.fund(XRP(1000), G1, alice, bob);
        env.close();

        env.trust(G1["USD"](100), bob);
        env.trust(G1["USD"](205), alice);
        env.close();

        env(pay(G1, bob, G1["USD"](10)));
        env(pay(G1, alice, G1["USD"](205)));
        env.close();

        AMM ammAlice(env, alice, XRP(500), G1["USD"](105));

        {
            auto lines = getAccountLines(env, bob);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == G1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "100");
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "10");
        }

        {
            auto lines = getAccountLines(env, alice, G1["USD"]);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == G1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "205");
            // 105 transferred to AMM
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "100");
        }

        {
            // Account with line unfrozen (proving operations normally work)
            //   test: can make Payment on that line
            env(pay(alice, bob, G1["USD"](1)));

            //   test: can receive Payment on that line
            env(pay(bob, alice, G1["USD"](1)));
            env.close();
        }

        {
            // Is created via a TrustSet with SetFreeze flag
            //   test: sets LowFreeze | HighFreeze flags
            env(trust(G1, bob["USD"](0), tfSetFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                G1["USD"](0).value().getJson(JsonOptions::none));
            BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
            env.close();
        }

        {
            // Account with line frozen by issuer
            //    test: can buy more assets on that line
            env(offer(bob, G1["USD"](5), XRP(25)));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 4u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfHighLimit.fieldName] ==
                bob["USD"](100).value().getJson(JsonOptions::none));
            auto amt = STAmount{Issue{to_currency("USD"), noAccount()}, -15}
                           .value()
                           .getJson(JsonOptions::none);
            BEAST_EXPECT(ff[sfBalance.fieldName] == amt);
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(525), G1["USD"](100), ammAlice.tokens()));
        }

        {
            //    test: can not sell assets from that line
            env(offer(bob, XRP(1), G1["USD"](5)), ter(tecUNFUNDED_OFFER));

            //    test: can receive Payment on that line
            env(pay(alice, bob, G1["USD"](1)));

            //    test: can not make Payment from that line
            env(pay(bob, alice, G1["USD"](1)), ter(tecPATH_DRY));
        }

        {
            // check G1 account lines
            //    test: shows freeze
            auto lines = getAccountLines(env, G1);
            Json::Value bobLine;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == bob.human())
                {
                    bobLine = it;
                    break;
                }
            }
            if (!BEAST_EXPECT(bobLine))
                return;
            BEAST_EXPECT(bobLine[jss::freeze] == true);
            BEAST_EXPECT(bobLine[jss::balance] == "-16");
        }

        {
            //    test: shows freeze peer
            auto lines = getAccountLines(env, bob);
            Json::Value g1Line;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == G1.human())
                {
                    g1Line = it;
                    break;
                }
            }
            if (!BEAST_EXPECT(g1Line))
                return;
            BEAST_EXPECT(g1Line[jss::freeze_peer] == true);
            BEAST_EXPECT(g1Line[jss::balance] == "16");
        }

        {
            // Is cleared via a TrustSet with ClearFreeze flag
            //    test: sets LowFreeze | HighFreeze flags
            env(trust(G1, bob["USD"](0), tfClearFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                G1["USD"](0).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfLowFreeze));
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
            env.close();
        }
    }

    void
    testGlobalFreeze(FeatureBitset features)
    {
        testcase("Global Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(12000), G1);
        env.fund(XRP(1000), A1);
        env.fund(XRP(20000), A2, A3, A4);
        env.close();

        env.trust(G1["USD"](1200), A1);
        env.trust(G1["USD"](200), A2);
        env.trust(G1["BTC"](100), A3);
        env.trust(G1["BTC"](100), A4);
        env.close();

        env(pay(G1, A1, G1["USD"](1000)));
        env(pay(G1, A2, G1["USD"](100)));
        env(pay(G1, A3, G1["BTC"](100)));
        env(pay(G1, A4, G1["BTC"](100)));
        env.close();

        AMM ammG1(env, G1, XRP(10000), G1["USD"](100));
        env(offer(A1, XRP(10000), G1["USD"](100)), txflags(tfPassive));
        env(offer(A2, G1["USD"](100), XRP(10000)), txflags(tfPassive));
        env.close();

        {
            // Is toggled via AccountSet using SetFlag and ClearFlag
            //    test: SetFlag GlobalFreeze
            env.require(nflags(G1, asfGlobalFreeze));
            env(fset(G1, asfGlobalFreeze));
            env.require(flags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));

            //    test: ClearFlag GlobalFreeze
            env(fclear(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));
        }

        {
            // Account without GlobalFreeze (proving operations normally work)
            //    test: visible offers where taker_pays is unfrozen issuer
            auto offers = env.rpc(
                "book_offers",
                std::string("USD/") + G1.human(),
                "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
            std::set<std::string> accounts;
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(A2.human()) != std::end(accounts));

            //    test: visible offers where taker_gets is unfrozen issuer
            offers = env.rpc(
                "book_offers",
                "XRP",
                std::string("USD/") + G1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
            accounts.clear();
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(A1.human()) != std::end(accounts));
        }

        {
            // Offers/Payments
            //    test: assets can be bought on the market
            // env(offer(A3, G1["BTC"](1), XRP(1)));
            AMM ammA3(env, A3, G1["BTC"](1), XRP(1));

            //    test: assets can be sold on the market
            // AMM is bidirectional

            //    test: direct issues can be sent
            env(pay(G1, A2, G1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(A2, G1, G1["USD"](1)));

            //    test: via rippling can be sent
            env(pay(A2, A1, G1["USD"](1)));

            //    test: via rippling can be sent back
            env(pay(A1, A2, G1["USD"](1)));
            ammA3.withdrawAll(std::nullopt);
        }

        {
            // Account with GlobalFreeze
            //  set GlobalFreeze first
            //    test: SetFlag GlobalFreeze will toggle back to freeze
            env.require(nflags(G1, asfGlobalFreeze));
            env(fset(G1, asfGlobalFreeze));
            env.require(flags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));

            //    test: assets can't be bought on the market
            AMM ammA3(env, A3, G1["BTC"](1), XRP(1), ter(tecFROZEN));

            //    test: assets can't be sold on the market
            // AMM is bidirectional
        }

        {
            //    test: book_offers shows offers
            //    (should these actually be filtered?)
            auto offers = env.rpc(
                "book_offers",
                "XRP",
                std::string("USD/") + G1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            offers = env.rpc(
                "book_offers",
                std::string("USD/") + G1.human(),
                "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
        }

        {
            // Payments
            //    test: direct issues can be sent
            env(pay(G1, A2, G1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(A2, G1, G1["USD"](1)));

            //    test: via rippling cant be sent
            env(pay(A2, A1, G1["USD"](1)), ter(tecPATH_DRY));
        }
    }

    void
    testOffersWhenFrozen(FeatureBitset features)
    {
        testcase("Offers for Frozen Trust Lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(2000), G1, A3, A4);
        env.fund(XRP(2000), A2);
        env.close();

        env.trust(G1["USD"](1000), A2);
        env.trust(G1["USD"](2000), A3);
        env.trust(G1["USD"](2001), A4);
        env.close();

        env(pay(G1, A3, G1["USD"](2000)));
        env(pay(G1, A4, G1["USD"](2001)));
        env.close();

        AMM ammA3(env, A3, XRP(1000), G1["USD"](1001));

        // removal after successful payment
        //    test: make a payment with partially consuming offer
        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
        env.close();

        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1001), G1["USD"](1000), ammA3.tokens()));

        //    test: someone else creates an offer providing liquidity
        env(offer(A4, XRP(999), G1["USD"](999)));
        env.close();
        // The offer consumes AMM offer
        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1000), G1["USD"](1001), ammA3.tokens()));

        //    test: AMM line is frozen
        auto const a3am =
            STAmount{Issue{to_currency("USD"), ammA3.ammAccount()}, 0};
        env(trust(G1, a3am, tfSetFreeze));
        auto const info = ammA3.ammRpcInfo();
        BEAST_EXPECT(info && (*info)[jss::amm][jss::asset2_frozen].asBool());
        auto affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
            return;
        auto ff =
            affected[0u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
        BEAST_EXPECT(
            ff[sfHighLimit.fieldName] == a3am.getJson(JsonOptions::none));
        BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
        BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
        env.close();

        //    test: Can make a payment via the new offer
        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
        env.close();
        // AMM is not consumed
        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1000), G1["USD"](1001), ammA3.tokens()));

        // removal buy successful OfferCreate
        //    test: freeze the new offer
        env(trust(G1, A4["USD"](0), tfSetFreeze));
        affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
            return;
        ff = affected[0u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
        BEAST_EXPECT(
            ff[sfLowLimit.fieldName] ==
            G1["USD"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
        BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
        env.close();

        //    test: can no longer create a crossing offer
        env(offer(A2, G1["USD"](999), XRP(999)));
        affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 8u)))
            return;
        auto created = affected[0u][sfCreatedNode.fieldName];
        BEAST_EXPECT(
            created[sfNewFields.fieldName][jss::Account] == A2.human());
        env.close();

        //    test: offer was removed by offer_create
        auto offers = getAccountOffers(env, A4)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
            return;
    }

    void
    testTxMultisign(FeatureBitset features)
    {
        testcase("Multisign AMM Transactions");

        using namespace jtx;
        Env env{*this, features};
        Account const bogie{"bogie", KeyType::secp256k1};
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const zelda{"zelda", KeyType::secp256k1};
        fund(env, gw, {alice, becky, zelda}, XRP(20000), {USD(20000)});

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), sig(alie));
        env.close();
        int const signerListOwners{features[featureMultiSignReserve] ? 2 : 5};
        env.require(owners(alice, signerListOwners + 0));

        // Multisign all AMM transactions
        AMM ammAlice(
            env,
            alice,
            XRP(10000),
            USD(10000),
            false,
            0,
            ammCrtFee(env).drops(),
            std::nullopt,
            std::nullopt,
            msig(becky, bogie),
            ter(tesSUCCESS));
        BEAST_EXPECT(
            ammAlice.expectBalances(XRP(10000), USD(10000), ammAlice.tokens()));

        ammAlice.deposit(alice, 1000000);
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(11000), USD(11000), IOUAmount{11000000, 0}));

        ammAlice.withdraw(alice, 1000000);
        ammAlice.expectBalances(XRP(10000), USD(10000), ammAlice.tokens());

        ammAlice.vote({}, 1000);
        BEAST_EXPECT(ammAlice.expectTradingFee(1000));

        ammAlice.bid(alice, 100);
        BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0));
        // 100 tokens burnt
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10000), USD(10000), IOUAmount{9999900, 0}));
    }

    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        // cannot have more than one offer with the same output issue

        Env env(*this, features);

        fund(env, gw, {alice, bob, carol}, XRP(10000), {USD(2000), EUR(1000)});

        AMM bobXRP_USD(env, bob, XRP(1000), USD(1000));
        AMM bobUSD_EUR(env, bob, USD(1000), EUR(1000));

        // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
        env(pay(alice, carol, USD(100)),
            path(~USD, ~EUR, ~USD),
            sendmax(XRP(200)),
            txflags(tfNoRippleDirect),
            ter(temBAD_PATH_LOOP));
    }

    void
    testRIPD1373(FeatureBitset features)
    {
        using namespace jtx;
        testcase("RIPD1373");

        {
            Env env(*this, features);
            auto const BobUSD = bob["USD"];
            auto const BobEUR = bob["EUR"];
            fund(env, gw, {alice, bob}, XRP(10000));
            env.trust(USD(1000), alice, bob);
            env.trust(EUR(1000), alice, bob);
            fund(
                env,
                bob,
                {alice, gw},
                {BobUSD(100), BobEUR(100)},
                Fund::IOUOnly);

            AMM ammBobXRP_USD(env, bob, XRP(100), BobUSD(100));
            env(offer(gw, XRP(100), USD(100)), txflags(tfPassive));

            AMM ammBobUSD_EUR(env, bob, BobUSD(100), BobEUR(100));
            env(offer(gw, USD(100), EUR(100)), txflags(tfPassive));

            Path const p = [&] {
                Path result;
                result.push_back(allpe(gw, BobUSD));
                result.push_back(cpe(EUR.currency));
                return result;
            }();

            PathSet paths(p);

            env(pay(alice, alice, EUR(1)),
                json(paths.json()),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10000), {USD(100)});

            AMM ammBob(env, bob, XRP(100), USD(100));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10000), {USD(100)});

            AMM ammBob(env, bob, XRP(100), USD(100));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
                sendmax(XRP(200)),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_MAX));
        }
    }

    void
    testLoop(FeatureBitset features)
    {
        testcase("test loop");
        using namespace jtx;

        auto const CNY = gw["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));
            env(pay(gw, alice, USD(100)));

            AMM ammBob(env, bob, XRP(100), USD(100));

            // payment path: USD -> USD/XRP -> XRP/USD
            env(pay(alice, carol, USD(100)),
                sendmax(USD(100)),
                path(~XRP, ~USD),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);
            env.trust(EUR(10000), alice, bob, carol);
            env.trust(CNY(10000), alice, bob, carol);

            env(pay(gw, bob, USD(200)));
            env(pay(gw, bob, EUR(200)));
            env(pay(gw, bob, CNY(100)));

            AMM ammBobXRP_USD(env, bob, XRP(100), USD(100));
            AMM ammBobUSD_EUR(env, bob, USD(100), EUR(100));
            AMM ammBobEUR_CNY(env, bob, EUR(100), CNY(100));

            // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
            env(pay(alice, carol, CNY(100)),
                sendmax(XRP(100)),
                path(~USD, ~EUR, ~USD, ~CNY),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
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
    testFlow()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const ownerPaysFee{featureOwnerPaysFee};

        testFalseDry(all);
        testBookStep(all);
        testBookStep(all | ownerPaysFee);
        testTransferRate(all | ownerPaysFee);
        testLimitQuality();
        testXRPPathLoop();
    }

    void
    testCrossingLimits()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        testStepLimit(all);
    }

    void
    testDeliverMin()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        test_convert_all_of_an_asset(all);
    }

    void
    testDepositAuth()
    {
        auto const supported{jtx::supported_amendments()};
        testPayment(supported - featureDepositPreauth);
        testPayment(supported);
        testPayIOU();
    }

    void
    testFreeze()
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testRippleState(sa);
        testGlobalFreeze(sa);
        testOffersWhenFrozen(sa);
    }

    void
    testMultisign()
    {
        using namespace jtx;
        auto const all = supported_amendments();

        testTxMultisign(
            all - featureMultiSignReserve - featureExpandedSignerList);
        testTxMultisign(all - featureExpandedSignerList);
        testTxMultisign(all);
    }

    void
    testPayStrand()
    {
        using namespace jtx;
        auto const all = supported_amendments();

        testToStrand(all);
        testRIPD1373(all);
        testLoop(all);
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
        testFlags();
    }

    void
    run() override
    {
        testCore();
        testOffers();
        testPaths();
        testFlow();
        testCrossingLimits();
        testDeliverMin();
        testDepositAuth();
        testFreeze();
        testMultisign();
        testPayStrand();
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

class AMMPerf_test : public beast::unit_test::suite
{
public:
    void
    testSwapPerformance()
    {
        testcase("Swap1");
        using namespace jtx;

        std::uint16_t tfee = 1000;
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const GBP = gw["GBP"];
        auto const issueIn = USD;
        auto const issueOut = GBP;
        auto const in = STAmount{USD, 1000};
        auto const out = STAmount{GBP, 1000};
        auto const assetIn = STAmount{USD, 1};
        auto const assetOut = STAmount{GBP, 1};

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i)
        {
            auto const res = swapAssetIn(Amounts{in, out}, assetIn, 0);
            (void)res;
        }
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        std::cout << "Number(swapIn) math: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         elapsed)
                         .count()
                  << std::endl;

        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i)
        {
            auto const n1 = STAmount{1};
            auto const feeMult =
                n1 - divide(STAmount{tfee}, STAmount{100000}, n1.issue());
            auto const en = multiply(in, out, issueOut);
            auto const den = in + multiply(assetIn, feeMult, issueIn);
            auto const res = out - divide(en, den, issueOut);
            (void)res;
        }
        elapsed = std::chrono::high_resolution_clock::now() - start;
        std::cout << "STAmount(swapIn) math: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         elapsed)
                         .count()
                  << std::endl;

        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i)
        {
            auto const res = swapAssetOut(Amounts{in, out}, assetOut, 0);
            (void)res;
        }
        elapsed = std::chrono::high_resolution_clock::now() - start;
        std::cout << "Number(swapOut) math: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         elapsed)
                         .count()
                  << std::endl;

        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i)
        {
            auto const n1 = STAmount{1};
            auto const feeMult =
                n1 - divide(STAmount{tfee}, STAmount{100000}, n1.issue());
            auto const en = multiply(in, out, issueIn);
            auto const den = out + assetOut;
            auto const res =
                divide(divide(en, den, issueIn) - in, feeMult, issueIn);
            (void)res;
        }
        elapsed = std::chrono::high_resolution_clock::now() - start;
        std::cout << "STAmount(swapOut) math: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         elapsed)
                         .count()
                  << std::endl;

        BEAST_EXPECT(true);
    }

    void
    run() override
    {
        testSwapPerformance();
    }
};

class AMMFib_test : public Test
{
public:
    void
    run() override
    {
        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            AMMContext ammCtx(alice, true);
            AMMLiquidity<STAmount, STAmount> ammLiquidity(
                *env.current(),
                ammAlice.ammAccount(),
                0,
                USD,
                XRP,
                ammCtx,
                env.journal);

            for (int nIters = 0; nIters < 10; ++nIters)
            {
                auto const offer =
                    ammLiquidity.getOffer(*env.current(), std::nullopt)
                        ->amount();
                std::cout << ammCtx.curIters() << " "
                          << to_string(offer.in.iou()) << " " << offer.out.xrp()
                          << std::endl;
                ammCtx.setAMMUsed();
                ammCtx.update();
            }
        });
    }
};

BEAST_DEFINE_TESTSUITE(AMM, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMMCalc, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMMPerf, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMMFib, app, ripple);

}  // namespace test
}  // namespace ripple
