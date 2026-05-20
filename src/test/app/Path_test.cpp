#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/domain.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/rate.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpld/core/Config.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl::test {

//------------------------------------------------------------------------------

json::Value
rpf(jtx::Account const& src, jtx::Account const& dst, std::uint32_t numSrc)
{
    json::Value jv = json::ValueType::Object;
    jv[jss::command] = "ripple_path_find";
    jv[jss::source_account] = toBase58(src);

    if (numSrc > 0)
    {
        auto& sc = (jv[jss::source_currencies] = json::ValueType::Array);
        json::Value j = json::ValueType::Object;
        while ((numSrc--) != 0u)
        {
            j[jss::currency] = std::to_string(numSrc + 100);
            sc.append(j);
        }
    }

    auto const d = toBase58(dst);
    jv[jss::destination_account] = d;

    json::Value& j = (jv[jss::destination_amount] = json::ValueType::Object);
    j[jss::currency] = "USD";
    j[jss::value] = "0.01";
    j[jss::issuer] = d;

    return jv;
}

//------------------------------------------------------------------------------

class Path_test : public beast::unit_test::Suite
{
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

public:
    class Gate
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
        waitFor(std::chrono::duration<Rep, Period> const& relTime)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, relTime, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        void
        signal()
        {
            std::scoped_lock const lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    auto
    findPathsRequest(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt,
        std::optional<uint256> const& domain = std::nullopt)
    {
        using namespace jtx;

        auto& app = env.app();
        Resource::Charge loadType = Resource::kFeeReferenceRpc;
        Resource::Consumer c;

        RPC::JsonContext context{
            {.j = env.journal,
             .app = app,
             .loadType = loadType,
             .netOps = app.getOPs(),
             .ledgerMaster = app.getLedgerMaster(),
             .consumer = c,
             .role = Role::USER,
             .coro = {},
             .infoSub = {},
             .apiVersion = RPC::kApiVersionIfUnspecified},
            {},
            {}};

        json::Value params = json::ValueType::Object;
        params[jss::command] = "ripple_path_find";
        params[jss::source_account] = toBase58(src);
        params[jss::destination_account] = toBase58(dst);
        params[jss::destination_amount] = saDstAmount.getJson(JsonOptions::Values::None);
        if (saSendMax)
            params[jss::send_max] = saSendMax->getJson(JsonOptions::Values::None);
        if (saSrcCurrency)
        {
            auto& sc = params[jss::source_currencies] = json::ValueType::Array;
            json::Value j = json::ValueType::Object;
            j[jss::currency] = to_string(saSrcCurrency.value());
            sc.append(j);
        }
        if (domain)
            params[jss::domain] = to_string(*domain);

        json::Value result;
        Gate g;
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = std::move(params);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });

        using namespace std::chrono_literals;
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(!result.isMember(jss::error));
        return result;
    }

    std::tuple<STPathSet, STAmount, STAmount>
    findPaths(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt,
        std::optional<uint256> const& domain = std::nullopt)
    {
        json::Value result =
            findPathsRequest(env, src, dst, saDstAmount, saSendMax, saSrcCurrency, domain);
        BEAST_EXPECT(!result.isMember(jss::error));

        STAmount da;
        if (result.isMember(jss::destination_amount))
            da = amountFromJson(kSfGeneric, result[jss::destination_amount]);

        STAmount sa;
        STPathSet paths;
        if (result.isMember(jss::alternatives))
        {
            auto const& alts = result[jss::alternatives];
            if (alts.size() > 0)
            {
                auto const& path = alts[0u];

                if (path.isMember(jss::source_amount))
                    sa = amountFromJson(kSfGeneric, path[jss::source_amount]);

                if (path.isMember(jss::destination_amount))
                    da = amountFromJson(kSfGeneric, path[jss::destination_amount]);

                if (path.isMember(jss::paths_computed))
                {
                    json::Value p;
                    p["Paths"] = path[jss::paths_computed];
                    STParsedJSONObject po("generic", p);

                    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                    paths = po.object->getFieldPathSet(sfPaths);
                }
            }
        }

        return std::make_tuple(std::move(paths), std::move(sa), std::move(da));
    }

    void
    sourceCurrenciesLimit()
    {
        testcase("source currency limits");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        env.fund(XRP(10000), "alice", "bob", gw);
        env.close();
        env.trust(gw["USD"](100), "alice", "bob");
        env.close();

        auto& app = env.app();
        Resource::Charge loadType = Resource::kFeeReferenceRpc;
        Resource::Consumer c;

        RPC::JsonContext context{
            {.j = env.journal,
             .app = app,
             .loadType = loadType,
             .netOps = app.getOPs(),
             .ledgerMaster = app.getLedgerMaster(),
             .consumer = c,
             .role = Role::USER,
             .coro = {},
             .infoSub = {},
             .apiVersion = RPC::kApiVersionIfUnspecified},
            {},
            {}};
        json::Value result;
        Gate g;
        // Test RPC::Tuning::max_src_cur source currencies.
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = rpf(Account("alice"), Account("bob"), RPC::Tuning::kMaxSrcCur);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(!result.isMember(jss::error));

        // Test more than RPC::Tuning::max_src_cur source currencies.
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = rpf(Account("alice"), Account("bob"), RPC::Tuning::kMaxSrcCur + 1);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(result.isMember(jss::error));

        // Test RPC::Tuning::max_auto_src_cur source currencies.
        for (auto i = 0; i < (RPC::Tuning::kMaxAutoSrcCur - 1); ++i)
            env.trust(Account("alice")[std::to_string(i + 100)](100), "bob");
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = rpf(Account("alice"), Account("bob"), 0);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(!result.isMember(jss::error));

        // Test more than RPC::Tuning::max_auto_src_cur source currencies.
        env.trust(Account("alice")["AUD"](100), "bob");
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = rpf(Account("alice"), Account("bob"), 0);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(result.isMember(jss::error));
    }

    void
    noDirectPathNoIntermediaryNoAlternatives()
    {
        testcase("no direct path no intermediary no alternatives");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob");
        env.close();

        auto const result = findPaths(env, "alice", "bob", Account("bob")["USD"](5));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    directPathNoIntermediary()
    {
        testcase("direct path no intermediary");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        env.trust(Account("alice")["USD"](700), "bob");

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = findPaths(env, "alice", "bob", Account("bob")["USD"](5));
        BEAST_EXPECT(st.empty());
        BEAST_EXPECT(equal(sa, Account("alice")["USD"](5)));
    }

    void
    paymentAutoPathFind()
    {
        testcase("payment auto path find");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.close();
        env.trust(usd(600), "alice");
        env.trust(usd(700), "bob");
        env(pay(gw, "alice", usd(70)));
        env(pay("alice", "bob", usd(24)));
        env.require(Balance("alice", usd(46)));
        env.require(Balance(gw, Account("alice")["USD"](-46)));
        env.require(Balance("bob", usd(24)));
        env.require(Balance(gw, Account("bob")["USD"](-24)));
    }

    void
    pathFind(bool const domainEnabled)
    {
        testcase(std::string("path find") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.close();
        env.trust(usd(600), "alice");
        env.trust(usd(700), "bob");
        env(pay(gw, "alice", usd(70)));
        env(pay(gw, "bob", usd(50)));

        std::optional<uint256> domainID;
        if (domainEnabled)
            domainID = setupDomain(env, {"alice", "bob", gw});

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = findPaths(
            env, "alice", "bob", Account("bob")["USD"](5), std::nullopt, std::nullopt, domainID);
        BEAST_EXPECT(same(st, stpath("gateway")));
        BEAST_EXPECT(equal(sa, Account("alice")["USD"](5)));
    }

    void
    xrpToXrp(bool const domainEnabled)
    {
        using namespace jtx;
        testcase(std::string("XRP to XRP") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob");
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
            domainID = setupDomain(env, {"alice", "bob"});

        auto const result =
            findPaths(env, "alice", "bob", XRP(5), std::nullopt, std::nullopt, domainID);
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    pathFindConsumeAll(bool const domainEnabled)
    {
        testcase(
            std::string("path find consume all") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;

        {
            Env env = pathTestEnv();
            env.fund(XRP(10000), "alice", "bob", "carol", "dan", "edward");
            env.close();
            env.trust(Account("alice")["USD"](10), "bob");
            env.trust(Account("bob")["USD"](10), "carol");
            env.trust(Account("carol")["USD"](10), "edward");
            env.trust(Account("alice")["USD"](100), "dan");
            env.trust(Account("dan")["USD"](100), "edward");

            std::optional<uint256> domainID;
            if (domainEnabled)
                domainID = setupDomain(env, {"alice", "bob", "carol", "dan", "edward"});

            STPathSet st;
            STAmount sa;
            STAmount da;
            std::tie(st, sa, da) = findPaths(
                env,
                "alice",
                "edward",
                Account("edward")["USD"](-1),
                std::nullopt,
                std::nullopt,
                domainID);
            BEAST_EXPECT(same(st, stpath("dan"), stpath("bob", "carol")));
            BEAST_EXPECT(equal(sa, Account("alice")["USD"](110)));
            BEAST_EXPECT(equal(da, Account("edward")["USD"](110)));
        }

        {
            Env env = pathTestEnv();
            auto const gw = Account("gateway");
            auto const usd = gw["USD"];
            env.fund(XRP(10000), "alice", "bob", "carol", gw);
            env.close();
            env.trust(usd(100), "bob", "carol");
            env.close();
            env(pay(gw, "carol", usd(100)));
            env.close();

            std::optional<uint256> domainID;
            if (domainEnabled)
            {
                domainID = setupDomain(env, {"alice", "bob", "carol", "gateway"});
                env(offer("carol", XRP(100), usd(100)), Domain(*domainID));
            }
            else
            {
                env(offer("carol", XRP(100), usd(100)));
            }
            env.close();

            STPathSet st;
            STAmount sa;
            STAmount da;
            std::tie(st, sa, da) = findPaths(
                env,
                "alice",
                "bob",
                Account("bob")["AUD"](-1),
                std::optional<STAmount>(XRP(1000000)),
                std::nullopt,
                domainID);
            BEAST_EXPECT(st.empty());
            std::tie(st, sa, da) = findPaths(
                env,
                "alice",
                "bob",
                Account("bob")["USD"](-1),
                std::optional<STAmount>(XRP(1000000)),
                std::nullopt,
                domainID);
            BEAST_EXPECT(sa == XRP(100));
            BEAST_EXPECT(equal(da, Account("bob")["USD"](100)));

            // if domain is used, finding path in the open offerbook will return
            // empty result
            if (domainEnabled)
            {
                std::tie(st, sa, da) = findPaths(
                    env,
                    "alice",
                    "bob",
                    Account("bob")["USD"](-1),
                    std::optional<STAmount>(XRP(1000000)),
                    std::nullopt,
                    std::nullopt);  // not specifying a domain
                BEAST_EXPECT(st.empty());
            }
        }
    }

    void
    alternativePathConsumeBoth(bool const domainEnabled)
    {
        testcase(
            std::string("alternative path consume both") + (domainEnabled ? " w/ " : " w/o ") +
            "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2Usd = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", gw, gw2);
        env.close();
        env.trust(usd(600), "alice");
        env.trust(gw2Usd(800), "alice");
        env.trust(usd(700), "bob");
        env.trust(gw2Usd(900), "bob");

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {"alice", "bob", "gateway", "gateway2"});
            env(pay(gw, "alice", usd(70)), Domain(*domainID));
            env(pay(gw2, "alice", gw2Usd(70)), Domain(*domainID));
            env(pay("alice", "bob", Account("bob")["USD"](140)),
                Paths(Account("alice")["USD"]),
                Domain(*domainID));
        }
        else
        {
            env(pay(gw, "alice", usd(70)));
            env(pay(gw2, "alice", gw2Usd(70)));
            env(pay("alice", "bob", Account("bob")["USD"](140)), Paths(Account("alice")["USD"]));
        }

        env.require(Balance("alice", usd(0)));
        env.require(Balance("alice", gw2Usd(0)));
        env.require(Balance("bob", usd(70)));
        env.require(Balance("bob", gw2Usd(70)));
        env.require(Balance(gw, Account("alice")["USD"](0)));
        env.require(Balance(gw, Account("bob")["USD"](-70)));
        env.require(Balance(gw2, Account("alice")["USD"](0)));
        env.require(Balance(gw2, Account("bob")["USD"](-70)));
    }

    void
    alternativePathsConsumeBestTransfer(bool const domainEnabled)
    {
        testcase(
            std::string("alternative paths consume best transfer") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2Usd = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", gw, gw2);
        env.close();
        env(rate(gw2, 1.1));
        env.trust(usd(600), "alice");
        env.trust(gw2Usd(800), "alice");
        env.trust(usd(700), "bob");
        env.trust(gw2Usd(900), "bob");

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {"alice", "bob", "gateway", "gateway2"});
            env(pay(gw, "alice", usd(70)), Domain(*domainID));
            env(pay(gw2, "alice", gw2Usd(70)), Domain(*domainID));
            env(pay("alice", "bob", usd(70)), Domain(*domainID));
        }
        else
        {
            env(pay(gw, "alice", usd(70)));
            env(pay(gw2, "alice", gw2Usd(70)));
            env(pay("alice", "bob", usd(70)));
        }
        env.require(Balance("alice", usd(0)));
        env.require(Balance("alice", gw2Usd(70)));
        env.require(Balance("bob", usd(70)));
        env.require(Balance("bob", gw2Usd(0)));
        env.require(Balance(gw, Account("alice")["USD"](0)));
        env.require(Balance(gw, Account("bob")["USD"](-70)));
        env.require(Balance(gw2, Account("alice")["USD"](-70)));
        env.require(Balance(gw2, Account("bob")["USD"](0)));
    }

    void
    alternativePathsConsumeBestTransferFirst()
    {
        testcase("alternative paths - consume best transfer first");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2Usd = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", gw, gw2);
        env.close();
        env(rate(gw2, 1.1));
        env.trust(usd(600), "alice");
        env.trust(gw2Usd(800), "alice");
        env.trust(usd(700), "bob");
        env.trust(gw2Usd(900), "bob");
        env(pay(gw, "alice", usd(70)));
        env(pay(gw2, "alice", gw2Usd(70)));
        env(pay("alice", "bob", Account("bob")["USD"](77)),
            Sendmax(Account("alice")["USD"](100)),
            Paths(Account("alice")["USD"]));
        env.require(Balance("alice", usd(0)));
        env.require(Balance("alice", gw2Usd(62.3)));
        env.require(Balance("bob", usd(70)));
        env.require(Balance("bob", gw2Usd(7)));
        env.require(Balance(gw, Account("alice")["USD"](0)));
        env.require(Balance(gw, Account("bob")["USD"](-70)));
        env.require(Balance(gw2, Account("alice")["USD"](-62.3)));
        env.require(Balance(gw2, Account("bob")["USD"](-7)));
    }

    void
    alternativePathsLimitReturnedPathsToBestQuality(bool const domainEnabled)
    {
        testcase(
            std::string("alternative paths - limit returned paths to best quality") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2Usd = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", "carol", "dan", gw, gw2);
        env.close();
        env(rate("carol", 1.1));
        env.trust(Account("carol")["USD"](800), "alice", "bob");
        env.trust(Account("dan")["USD"](800), "alice", "bob");
        env.trust(usd(800), "alice", "bob");
        env.trust(gw2Usd(800), "alice", "bob");
        env.trust(Account("alice")["USD"](800), "dan");
        env.trust(Account("bob")["USD"](800), "dan");
        env.close();
        env(pay(gw2, "alice", gw2Usd(100)));
        env.close();
        env(pay("carol", "alice", Account("carol")["USD"](100)));
        env.close();
        env(pay(gw, "alice", usd(100)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {"alice", "bob", "carol", "dan", gw, gw2});
        }

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = findPaths(
            env, "alice", "bob", Account("bob")["USD"](5), std::nullopt, std::nullopt, domainID);
        BEAST_EXPECT(
            same(st, stpath("gateway"), stpath("gateway2"), stpath("dan"), stpath("carol")));
        BEAST_EXPECT(equal(sa, Account("alice")["USD"](5)));
    }

    void
    issuesPathNegativeIssue(bool const domainEnabled)
    {
        testcase(
            std::string("path negative: Issue #5") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob", "carol", "dan");
        env.close();
        env.trust(Account("bob")["USD"](100), "alice", "carol", "dan");
        env.trust(Account("alice")["USD"](100), "dan");
        env.trust(Account("carol")["USD"](100), "dan");
        env(pay("bob", "carol", Account("bob")["USD"](75)));
        env.require(Balance("bob", Account("carol")["USD"](-75)));
        env.require(Balance("carol", Account("bob")["USD"](75)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {"alice", "bob", "carol", "dan"});
        }

        auto result = findPaths(
            env, "alice", "bob", Account("bob")["USD"](25), std::nullopt, std::nullopt, domainID);
        BEAST_EXPECT(std::get<0>(result).empty());

        env(pay("alice", "bob", Account("alice")["USD"](25)), Ter(tecPATH_DRY));
        env.close();

        result = findPaths(
            env, "alice", "bob", Account("alice")["USD"](25), std::nullopt, std::nullopt, domainID);
        BEAST_EXPECT(std::get<0>(result).empty());

        env.require(Balance("alice", Account("bob")["USD"](0)));
        env.require(Balance("alice", Account("dan")["USD"](0)));
        env.require(Balance("bob", Account("alice")["USD"](0)));
        env.require(Balance("bob", Account("carol")["USD"](-75)));
        env.require(Balance("bob", Account("dan")["USD"](0)));
        env.require(Balance("carol", Account("bob")["USD"](75)));
        env.require(Balance("carol", Account("dan")["USD"](0)));
        env.require(Balance("dan", Account("alice")["USD"](0)));
        env.require(Balance("dan", Account("bob")["USD"](0)));
        env.require(Balance("dan", Account("carol")["USD"](0)));
    }

    // alice_ -- limit 40 --> bob_
    // alice_ --> carol_ --> dan --> bob_
    // Balance of 100 USD Bob - Balance of 37 USD -> Rod
    void
    issuesPathNegativeRippleClientIssue23Smaller()
    {
        testcase("path negative: ripple-client issue #23: smaller");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob", "carol", "dan");
        env.close();
        env.trust(Account("alice")["USD"](40), "bob");
        env.trust(Account("dan")["USD"](20), "bob");
        env.trust(Account("alice")["USD"](20), "carol");
        env.trust(Account("carol")["USD"](20), "dan");
        env(pay("alice", "bob", Account("bob")["USD"](55)), Paths(Account("alice")["USD"]));
        env.require(Balance("bob", Account("alice")["USD"](40)));
        env.require(Balance("bob", Account("dan")["USD"](15)));
    }

    // alice_ -120 USD-> edward -25 USD-> bob_
    // alice_ -25 USD-> carol_ -75 USD -> dan -100 USD-> bob_
    void
    issuesPathNegativeRippleClientIssue23Larger()
    {
        testcase("path negative: ripple-client issue #23: larger");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob", "carol", "dan", "edward");
        env.close();
        env.trust(Account("alice")["USD"](120), "edward");
        env.trust(Account("edward")["USD"](25), "bob");
        env.trust(Account("dan")["USD"](100), "bob");
        env.trust(Account("alice")["USD"](25), "carol");
        env.trust(Account("carol")["USD"](75), "dan");
        env(pay("alice", "bob", Account("bob")["USD"](50)), Paths(Account("alice")["USD"]));
        env.require(Balance("alice", Account("edward")["USD"](-25)));
        env.require(Balance("alice", Account("carol")["USD"](-25)));
        env.require(Balance("bob", Account("edward")["USD"](25)));
        env.require(Balance("bob", Account("dan")["USD"](25)));
        env.require(Balance("carol", Account("alice")["USD"](25)));
        env.require(Balance("carol", Account("dan")["USD"](-25)));
        env.require(Balance("dan", Account("carol")["USD"](25)));
        env.require(Balance("dan", Account("bob")["USD"](-25)));
    }

    // carol_ holds gateway AUD, sells gateway AUD for XRP
    // bob_ will hold gateway AUD
    // alice_ pays bob_ gateway AUD using XRP
    void
    viaOffersViaGateway(bool const domainEnabled)
    {
        testcase(std::string("via gateway") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const aud = gw["AUD"];
        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        env.close();
        env(rate(gw, 1.1));
        env.close();
        env.trust(aud(100), "bob", "carol");
        env.close();
        env(pay(gw, "carol", aud(50)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {"alice", "bob", "carol", gw});
            env(offer("carol", XRP(50), aud(50)), Domain(*domainID));
            env.close();
            env(pay("alice", "bob", aud(10)), Sendmax(XRP(100)), Paths(XRP), Domain(*domainID));
            env.close();
        }
        else
        {
            env(offer("carol", XRP(50), aud(50)));
            env.close();
            env(pay("alice", "bob", aud(10)), Sendmax(XRP(100)), Paths(XRP));
            env.close();
        }

        env.require(Balance("bob", aud(10)));
        env.require(Balance("carol", aud(39)));

        auto const result = findPaths(
            env, "alice", "bob", Account("bob")["USD"](25), std::nullopt, std::nullopt, domainID);
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    indirectPathsPathFind()
    {
        testcase("path find");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob", "carol");
        env.close();
        env.trust(Account("alice")["USD"](1000), "bob");
        env.trust(Account("bob")["USD"](1000), "carol");

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) =
            findPaths(env, "alice", "carol", Account("carol")["USD"](5));
        BEAST_EXPECT(same(st, stpath("bob")));
        BEAST_EXPECT(equal(sa, Account("alice")["USD"](5)));
    }

    void
    qualityPathsQualitySetAndTest()
    {
        testcase("quality set and test");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        env(trust("bob", Account("alice")["USD"](1000)),
            Json("{\"" + sfQualityIn.fieldName + "\": 2000}"),
            Json("{\"" + sfQualityOut.fieldName + "\": 1400000000}"));

        json::Value jv;
        json::Reader().parse(
            R"({
                "Balance" : {
                    "currency" : "USD",
                    "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
                    "value" : "0"
                },
                "Flags" : 131072,
                "HighLimit" : {
                    "currency" : "USD",
                    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                    "value" : "1000"
                },
                "HighNode" : "0",
                "HighQualityIn" : 2000,
                "HighQualityOut" : 1400000000,
                "LedgerEntryType" : "RippleState",
                "LowLimit" : {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "0"
                },
                "LowNode" : "0"
            })",
            jv);

        auto const jvL = env.le(keylet::line(Account("bob").id(), Account("alice")["USD"]))
                             ->getJson(JsonOptions::Values::None);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            BEAST_EXPECT(*it == jvL[it.memberName()]);
    }

    void
    trustAutoClearTrustNormalClear()
    {
        testcase("trust normal clear");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        env.trust(Account("bob")["USD"](1000), "alice");
        env.trust(Account("alice")["USD"](1000), "bob");

        json::Value jv;
        json::Reader().parse(
            R"({
                "Balance" : {
                    "currency" : "USD",
                    "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
                    "value" : "0"
                },
                "Flags" : 196608,
                "HighLimit" : {
                    "currency" : "USD",
                    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                    "value" : "1000"
                },
                "HighNode" : "0",
                "LedgerEntryType" : "RippleState",
                "LowLimit" : {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "1000"
                },
                "LowNode" : "0"
            })",
            jv);

        auto const jvL = env.le(keylet::line(Account("bob").id(), Account("alice")["USD"]))
                             ->getJson(JsonOptions::Values::None);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            BEAST_EXPECT(*it == jvL[it.memberName()]);

        env.trust(Account("bob")["USD"](0), "alice");
        env.trust(Account("alice")["USD"](0), "bob");
        BEAST_EXPECT(env.le(keylet::line(Account("bob").id(), Account("alice")["USD"])) == nullptr);
    }

    void
    trustAutoClearTrustAutoClear()
    {
        testcase("trust auto clear");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        env.trust(Account("bob")["USD"](1000), "alice");
        env(pay("bob", "alice", Account("bob")["USD"](50)));
        env.trust(Account("bob")["USD"](0), "alice");

        json::Value jv;
        json::Reader().parse(
            R"({
                "Balance" :
                {
                    "currency" : "USD",
                    "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
                    "value" : "50"
                },
                "Flags" : 65536,
                "HighLimit" :
                {
                    "currency" : "USD",
                    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                    "value" : "0"
                },
                "HighNode" : "0",
                "LedgerEntryType" : "RippleState",
                "LowLimit" :
                {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "0"
                },
                "LowNode" : "0"
            })",
            jv);

        auto const jvL = env.le(keylet::line(Account("alice").id(), Account("bob")["USD"]))
                             ->getJson(JsonOptions::Values::None);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            BEAST_EXPECT(*it == jvL[it.memberName()]);

        env(pay("alice", "bob", Account("alice")["USD"](50)));
        BEAST_EXPECT(env.le(keylet::line(Account("alice").id(), Account("bob")["USD"])) == nullptr);
    }

    void
    pathFind01(bool const domainEnabled)
    {
        testcase(
            std::string("Path Find: XRP -> XRP and XRP -> IOU") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account const g3{"G3"};
        Account const m1{"M1"};

        env.fund(XRP(100000), a1);
        env.fund(XRP(10000), a2);
        env.fund(XRP(1000), a3, g1, g2, g3, m1);
        env.close();

        env.trust(g1["XYZ"](5000), a1);
        env.trust(g3["ABC"](5000), a1);
        env.trust(g2["XYZ"](5000), a2);
        env.trust(g3["ABC"](5000), a2);
        env.trust(a2["ABC"](1000), a3);
        env.trust(g1["XYZ"](100000), m1);
        env.trust(g2["XYZ"](100000), m1);
        env.trust(g3["ABC"](100000), m1);
        env.close();

        env(pay(g1, a1, g1["XYZ"](3500)));
        env(pay(g3, a1, g3["ABC"](1200)));
        env(pay(g2, m1, g2["XYZ"](25000)));
        env(pay(g3, m1, g3["ABC"](25000)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {a1, a2, a3, g1, g2, g3, m1});
            env(offer(m1, g1["XYZ"](1000), g2["XYZ"](1000)), Domain(*domainID));
            env(offer(m1, XRP(10000), g3["ABC"](1000)), Domain(*domainID));
            env.close();
        }
        else
        {
            env(offer(m1, g1["XYZ"](1000), g2["XYZ"](1000)));
            env(offer(m1, XRP(10000), g3["ABC"](1000)));
            env.close();
        }

        STPathSet st;
        STAmount sa, da;

        {
            auto const& sendAmt = XRP(10);
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, xrpCurrency(), domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(st.empty());
        }

        {
            // no path should exist for this since dest account
            // does not exist.
            auto const& sendAmt = XRP(200);
            std::tie(st, sa, da) =
                findPaths(env, a1, Account{"A0"}, sendAmt, std::nullopt, xrpCurrency(), domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(st.empty());
        }

        {
            auto const& sendAmt = g3["ABC"](10);
            std::tie(st, sa, da) =
                findPaths(env, a2, g3, sendAmt, std::nullopt, xrpCurrency(), domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRP(100)));
            BEAST_EXPECT(same(st, stpath(ipe(g3["ABC"]))));
        }

        {
            auto const& sendAmt = a2["ABC"](1);
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, xrpCurrency(), domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRP(10)));
            BEAST_EXPECT(same(st, stpath(ipe(g3["ABC"]), g3)));
        }

        {
            auto const& sendAmt = a3["ABC"](1);
            std::tie(st, sa, da) =
                findPaths(env, a1, a3, sendAmt, std::nullopt, xrpCurrency(), domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRP(10)));
            BEAST_EXPECT(same(st, stpath(ipe(g3["ABC"]), g3, a2)));
        }
    }

    void
    pathFind02(bool const domainEnabled)
    {
        testcase(
            std::string("Path Find: non-XRP -> XRP") + (domainEnabled ? " w/ " : " w/o ") +
            "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const g3{"G3"};
        Account const m1{"M1"};

        env.fund(XRP(1000), a1, a2, g3);
        env.fund(XRP(11000), m1);
        env.close();

        env.trust(g3["ABC"](1000), a1, a2);
        env.trust(g3["ABC"](100000), m1);
        env.close();

        env(pay(g3, a1, g3["ABC"](1000)));
        env(pay(g3, a2, g3["ABC"](1000)));
        env(pay(g3, m1, g3["ABC"](1200)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {a1, a2, g3, m1});
            env(offer(m1, g3["ABC"](1000), XRP(10000)), Domain(*domainID));
        }
        else
        {
            env(offer(m1, g3["ABC"](1000), XRP(10000)));
        }

        STPathSet st;
        STAmount sa, da;
        auto const& sendAmt = XRP(10);

        {
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, a2["ABC"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["ABC"](1)));
            BEAST_EXPECT(same(st, stpath(g3, ipe(xrpIssue()))));
        }

        // domain offer will not be considered in pathfinding for non-domain
        // paths
        if (domainEnabled)
        {
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, a2["ABC"].currency);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(st.empty());
        }
    }

    void
    pathFind04(bool const domainEnabled)
    {
        testcase(
            std::string("Path Find: Bitstamp and SnapSwap, liquidity with no offers") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const g1Bs{"G1BS"};
        Account const g2Sw{"G2SW"};
        Account const m1{"M1"};

        env.fund(XRP(1000), g1Bs, g2Sw, a1, a2);
        env.fund(XRP(11000), m1);
        env.close();

        env.trust(g1Bs["HKD"](2000), a1);
        env.trust(g2Sw["HKD"](2000), a2);
        env.trust(g1Bs["HKD"](100000), m1);
        env.trust(g2Sw["HKD"](100000), m1);
        env.close();

        env(pay(g1Bs, a1, g1Bs["HKD"](1000)));
        env(pay(g2Sw, a2, g2Sw["HKD"](1000)));
        // SnapSwap wants to be able to set trust line quality settings so they
        // can charge a fee when transactions ripple across. Liquidity
        // provider, via trusting/holding both accounts
        env(pay(g1Bs, m1, g1Bs["HKD"](1200)));
        env(pay(g2Sw, m1, g2Sw["HKD"](5000)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
            domainID = setupDomain(env, {a1, a2, g1Bs, g2Sw, m1});

        STPathSet st;
        STAmount sa, da;

        {
            auto const& sendAmt = a2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, a2["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(g1Bs, m1, g2Sw)));
        }

        {
            auto const& sendAmt = a1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a2, a1, sendAmt, std::nullopt, a1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a2["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(g2Sw, m1, g1Bs)));
        }

        {
            auto const& sendAmt = a2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, g1Bs, a2, sendAmt, std::nullopt, a1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, g1Bs["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(m1, g2Sw)));
        }

        {
            auto const& sendAmt = m1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, m1, g1Bs, sendAmt, std::nullopt, a1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, m1["HKD"](10)));
            BEAST_EXPECT(st.empty());
        }

        {
            auto const& sendAmt = a1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, g2Sw, a1, sendAmt, std::nullopt, a1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, g2Sw["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(m1, g1Bs)));
        }
    }

    void
    pathFind05(bool const domainEnabled)
    {
        testcase(
            std::string("Path Find: non-XRP -> non-XRP, same currency") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const a4{"A4"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account const g3{"G3"};
        Account const g4{"G4"};
        Account const m1{"M1"};
        Account const m2{"M2"};

        env.fund(XRP(1000), a1, a2, a3, g1, g2, g3, g4);
        env.fund(XRP(10000), a4);
        env.fund(XRP(11000), m1, m2);
        env.close();

        env.trust(g1["HKD"](2000), a1);
        env.trust(g2["HKD"](2000), a2);
        env.trust(g1["HKD"](2000), a3);
        env.trust(g1["HKD"](100000), m1);
        env.trust(g2["HKD"](100000), m1);
        env.trust(g1["HKD"](100000), m2);
        env.trust(g2["HKD"](100000), m2);
        env.close();

        env(pay(g1, a1, g1["HKD"](1000)));
        env(pay(g2, a2, g2["HKD"](1000)));
        env(pay(g1, a3, g1["HKD"](1000)));
        env(pay(g1, m1, g1["HKD"](1200)));
        env(pay(g2, m1, g2["HKD"](5000)));
        env(pay(g1, m2, g1["HKD"](1200)));
        env(pay(g2, m2, g2["HKD"](5000)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {a1, a2, a3, a4, g1, g2, g3, g4, m1, m2});
            env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)), Domain(*domainID));
            env(offer(m2, XRP(10000), g2["HKD"](1000)), Domain(*domainID));
            env(offer(m2, g1["HKD"](1000), XRP(10000)), Domain(*domainID));
        }
        else
        {
            env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)));
            env(offer(m2, XRP(10000), g2["HKD"](1000)));
            env(offer(m2, g1["HKD"](1000), XRP(10000)));
        }

        STPathSet st;
        STAmount sa, da;

        {
            // A) Borrow or repay --
            //  Source -> Destination (repay source issuer)
            auto const& sendAmt = g1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, g1, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
        }

        {
            // A2) Borrow or repay --
            //  Source -> Destination (repay destination issuer)
            auto const& sendAmt = a1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, g1, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
        }

        {
            // B) Common gateway --
            //  Source -> AC -> Destination
            auto const& sendAmt = a3["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, a3, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(g1)));
        }

        {
            // C) Gateway to gateway --
            //  Source -> OB -> Destination
            auto const& sendAmt = g2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, g1, g2, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, g1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(ipe(g2["HKD"])),
                stpath(m1),
                stpath(m2),
                stpath(ipe(xrpIssue()), ipe(g2["HKD"]))));
        }

        {
            // D) User to unlinked gateway via order book --
            //  Source -> AC -> OB -> Destination
            auto const& sendAmt = g2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, g2, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(g1, m1),
                stpath(g1, m2),
                stpath(g1, ipe(g2["HKD"])),
                stpath(g1, ipe(xrpIssue()), ipe(g2["HKD"]))));
        }

        {
            // I4) XRP bridge" --
            //  Source -> AC -> OB to XRP -> OB from XRP -> AC ->
            //  Destination
            auto const& sendAmt = a2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(g1, m1, g2),
                stpath(g1, m2, g2),
                stpath(g1, ipe(g2["HKD"]), g2),
                stpath(g1, ipe(xrpIssue()), ipe(g2["HKD"]), g2)));
        }
    }

    void
    pathFind06(bool const domainEnabled)
    {
        testcase(
            std::string("Path Find: non-XRP -> non-XRP, same currency)") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account const m1{"M1"};

        env.fund(XRP(11000), m1);
        env.fund(XRP(1000), a1, a2, a3, g1, g2);
        env.close();

        env.trust(g1["HKD"](2000), a1);
        env.trust(g2["HKD"](2000), a2);
        env.trust(a2["HKD"](2000), a3);
        env.trust(g1["HKD"](100000), m1);
        env.trust(g2["HKD"](100000), m1);
        env.close();

        env(pay(g1, a1, g1["HKD"](1000)));
        env(pay(g2, a2, g2["HKD"](1000)));
        env(pay(g1, m1, g1["HKD"](5000)));
        env(pay(g2, m1, g2["HKD"](5000)));
        env.close();

        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {a1, a2, a3, g1, g2, m1});
            env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)), Domain(*domainID));
        }
        else
        {
            env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)));
        }

        // E) Gateway to user
        //  Source -> OB -> AC -> Destination
        auto const& sendAmt = a2["HKD"](10);
        STPathSet st;
        STAmount sa, da;
        std::tie(st, sa, da) =
            findPaths(env, g1, a2, sendAmt, std::nullopt, g1["HKD"].currency, domainID);
        BEAST_EXPECT(equal(da, sendAmt));
        BEAST_EXPECT(equal(sa, g1["HKD"](10)));
        BEAST_EXPECT(same(st, stpath(m1, g2), stpath(ipe(g2["HKD"]), g2)));
    }

    void
    receiveMax(bool const domainEnabled)
    {
        testcase(std::string("Receive max") + (domainEnabled ? " w/ " : " w/o ") + "domain");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        {
            // XRP -> IOU receive max
            Env env = pathTestEnv();
            env.fund(XRP(10000), alice, bob, charlie, gw);
            env.close();
            env.trust(usd(100), alice, bob, charlie);
            env.close();
            env(pay(gw, charlie, usd(10)));
            env.close();

            std::optional<uint256> domainID;
            if (domainEnabled)
            {
                domainID = setupDomain(env, {alice, bob, charlie, gw});
                env(offer(charlie, XRP(10), usd(10)), Domain(*domainID));
                env.close();
            }
            else
            {
                env(offer(charlie, XRP(10), usd(10)));
                env.close();
            }

            auto [st, sa, da] =
                findPaths(env, alice, bob, usd(-1), XRP(100).value(), std::nullopt, domainID);
            BEAST_EXPECT(sa == XRP(10));
            BEAST_EXPECT(equal(da, usd(10)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getCurrency() == usd.currency);
            }
        }
        {
            // IOU -> XRP receive max
            Env env = pathTestEnv();
            env.fund(XRP(10000), alice, bob, charlie, gw);
            env.close();
            env.trust(usd(100), alice, bob, charlie);
            env.close();
            env(pay(gw, alice, usd(10)));
            env.close();

            std::optional<uint256> domainID;
            if (domainEnabled)
            {
                domainID = setupDomain(env, {alice, bob, charlie, gw});
                env(offer(charlie, usd(10), XRP(10)), Domain(*domainID));
                env.close();
            }
            else
            {
                env(offer(charlie, usd(10), XRP(10)));
                env.close();
            }

            auto [st, sa, da] =
                findPaths(env, alice, bob, drops(-1), usd(100).value(), std::nullopt, domainID);
            BEAST_EXPECT(sa == usd(10));
            BEAST_EXPECT(equal(da, XRP(10)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == xrpAccount() &&
                    pathElem.getCurrency() == xrpCurrency());
            }
        }
    }

    void
    norippleCombinations()
    {
        using namespace jtx;
        // This test will create trust lines with various values of the noRipple
        // flag. alice_ <-> george <-> bob_ george will sort of act like a
        // gateway, but use a different name to avoid the usual assumptions
        // about gateways.
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const george = Account("george");
        auto const usd = george["USD"];
        auto test = [&](std::string casename, bool aliceRipple, bool bobRipple, bool expectPath) {
            testcase(casename);

            Env env = pathTestEnv();
            env.fund(XRP(10000), noripple(alice, bob, george));
            env.close();
            // Set the same flags at both ends of the trustline, even though
            // only george's matter.
            env(trust(alice, usd(100), aliceRipple ? tfClearNoRipple : tfSetNoRipple));
            env(trust(george, alice["USD"](100), aliceRipple ? tfClearNoRipple : tfSetNoRipple));
            env(trust(bob, usd(100), bobRipple ? tfClearNoRipple : tfSetNoRipple));
            env(trust(george, bob["USD"](100), bobRipple ? tfClearNoRipple : tfSetNoRipple));
            env.close();
            env(pay(george, alice, usd(70)));
            env.close();

            auto [st, sa, da] = findPaths(env, "alice", "bob", Account("bob")["USD"](5));
            BEAST_EXPECT(equal(da, bob["USD"](5)));

            if (expectPath)
            {
                BEAST_EXPECT(st.size() == 1);
                BEAST_EXPECT(same(st, stpath("george")));
                BEAST_EXPECT(equal(sa, alice["USD"](5)));
            }
            else
            {
                BEAST_EXPECT(st.empty());
                BEAST_EXPECT(equal(sa, XRP(0)));
            }
        };
        test("ripple -> ripple", true, true, true);
        test("ripple -> no ripple", true, false, true);
        test("no ripple -> ripple", false, true, true);
        test("no ripple -> no ripple", false, false, false);
    }

    void
    hybridOfferPath()
    {
        testcase("Hybrid offer path");
        using namespace jtx;

        // test cases copied from path_find_05 and ensures path results for
        // different combinations of open/domain/hybrid offers. `func` is a
        // lambda param that creates different types of offers
        auto testPathfind = [&](auto func, bool const domainEnabled = false) {
            Env env = pathTestEnv();
            Account const a1{"A1"};
            Account const a2{"A2"};
            Account const a3{"A3"};
            Account const a4{"A4"};
            Account const g1{"G1"};
            Account const g2{"G2"};
            Account const g3{"G3"};
            Account const g4{"G4"};
            Account const m1{"M1"};
            Account const m2{"M2"};

            env.fund(XRP(1000), a1, a2, a3, g1, g2, g3, g4);
            env.fund(XRP(10000), a4);
            env.fund(XRP(11000), m1, m2);
            env.close();

            env.trust(g1["HKD"](2000), a1);
            env.trust(g2["HKD"](2000), a2);
            env.trust(g1["HKD"](2000), a3);
            env.trust(g1["HKD"](100000), m1);
            env.trust(g2["HKD"](100000), m1);
            env.trust(g1["HKD"](100000), m2);
            env.trust(g2["HKD"](100000), m2);
            env.close();

            env(pay(g1, a1, g1["HKD"](1000)));
            env(pay(g2, a2, g2["HKD"](1000)));
            env(pay(g1, a3, g1["HKD"](1000)));
            env(pay(g1, m1, g1["HKD"](1200)));
            env(pay(g2, m1, g2["HKD"](5000)));
            env(pay(g1, m2, g1["HKD"](1200)));
            env(pay(g2, m2, g2["HKD"](5000)));
            env.close();

            std::optional<uint256> domainID =
                setupDomain(env, {a1, a2, a3, a4, g1, g2, g3, g4, m1, m2});
            BEAST_EXPECT(domainID);

            func(env, m1, m2, g1, g2, *domainID);

            STPathSet st;
            STAmount sa, da;

            {
                // A) Borrow or repay --
                //  Source -> Destination (repay source issuer)
                auto const& sendAmt = g1["HKD"](10);
                std::tie(st, sa, da) = findPaths(
                    env,
                    a1,
                    g1,
                    sendAmt,
                    std::nullopt,
                    g1["HKD"].currency,
                    domainEnabled ? domainID : std::nullopt);
                BEAST_EXPECT(st.empty());
                BEAST_EXPECT(equal(da, sendAmt));
                BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            }

            {
                // A2) Borrow or repay --
                //  Source -> Destination (repay destination issuer)
                auto const& sendAmt = a1["HKD"](10);
                std::tie(st, sa, da) = findPaths(
                    env,
                    a1,
                    g1,
                    sendAmt,
                    std::nullopt,
                    g1["HKD"].currency,
                    domainEnabled ? domainID : std::nullopt);
                BEAST_EXPECT(st.empty());
                BEAST_EXPECT(equal(da, sendAmt));
                BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            }

            {
                // B) Common gateway --
                //  Source -> AC -> Destination
                auto const& sendAmt = a3["HKD"](10);
                std::tie(st, sa, da) = findPaths(
                    env,
                    a1,
                    a3,
                    sendAmt,
                    std::nullopt,
                    g1["HKD"].currency,
                    domainEnabled ? domainID : std::nullopt);
                BEAST_EXPECT(equal(da, sendAmt));
                BEAST_EXPECT(equal(sa, a1["HKD"](10)));
                BEAST_EXPECT(same(st, stpath(g1)));
            }

            {
                // C) Gateway to gateway --
                //  Source -> OB -> Destination
                auto const& sendAmt = g2["HKD"](10);
                std::tie(st, sa, da) = findPaths(
                    env,
                    g1,
                    g2,
                    sendAmt,
                    std::nullopt,
                    g1["HKD"].currency,
                    domainEnabled ? domainID : std::nullopt);
                BEAST_EXPECT(equal(da, sendAmt));
                BEAST_EXPECT(equal(sa, g1["HKD"](10)));
                BEAST_EXPECT(same(
                    st,
                    stpath(ipe(g2["HKD"])),
                    stpath(m1),
                    stpath(m2),
                    stpath(ipe(xrpIssue()), ipe(g2["HKD"]))));
            }

            {
                // D) User to unlinked gateway via order book --
                //  Source -> AC -> OB -> Destination
                auto const& sendAmt = g2["HKD"](10);
                std::tie(st, sa, da) = findPaths(
                    env,
                    a1,
                    g2,
                    sendAmt,
                    std::nullopt,
                    g1["HKD"].currency,
                    domainEnabled ? domainID : std::nullopt);
                BEAST_EXPECT(equal(da, sendAmt));
                BEAST_EXPECT(equal(sa, a1["HKD"](10)));
                BEAST_EXPECT(same(
                    st,
                    stpath(g1, m1),
                    stpath(g1, m2),
                    stpath(g1, ipe(g2["HKD"])),
                    stpath(g1, ipe(xrpIssue()), ipe(g2["HKD"]))));
            }

            {
                // I4) XRP bridge" --
                //  Source -> AC -> OB to XRP -> OB from XRP -> AC ->
                //  Destination
                auto const& sendAmt = a2["HKD"](10);
                std::tie(st, sa, da) = findPaths(
                    env,
                    a1,
                    a2,
                    sendAmt,
                    std::nullopt,
                    g1["HKD"].currency,
                    domainEnabled ? domainID : std::nullopt);
                BEAST_EXPECT(equal(da, sendAmt));
                BEAST_EXPECT(equal(sa, a1["HKD"](10)));
                BEAST_EXPECT(same(
                    st,
                    stpath(g1, m1, g2),
                    stpath(g1, m2, g2),
                    stpath(g1, ipe(g2["HKD"]), g2),
                    stpath(g1, ipe(xrpIssue()), ipe(g2["HKD"]), g2)));
            }
        };

        // the following tests exercise different combinations of open/hybrid
        // offers to make sure that hybrid offers work in pathfinding for open
        // order book
        {
            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)));
                });

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)));
                });

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                });

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                });

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                });
        }

        // the following tests exercise different combinations of domain/hybrid
        // offers to make sure that hybrid offers work in pathfinding for domain
        // order book
        {
            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)), Domain(domainID));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)), Domain(domainID));
                },
                true);

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)), Domain(domainID));
                },
                true);

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)), Domain(domainID));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)), Domain(domainID));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                },
                true);

            testPathfind(
                [](Env& env, Account m1, Account m2, Account g1, Account g2, uint256 domainID) {
                    env(offer(m1, g1["HKD"](1000), g2["HKD"](1000)), Domain(domainID));
                    env(offer(m2, XRP(10000), g2["HKD"](1000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                    env(offer(m2, g1["HKD"](1000), XRP(10000)),
                        Domain(domainID),
                        Txflags(tfHybrid));
                },
                true);
        }
    }

    void
    ammDomainPath()
    {
        testcase("AMM not used in domain path");
        using namespace jtx;
        Env env = pathTestEnv();
        PermissionedDEX const permDex(env);
        auto const& [gw_, domainOwner, alice_, bob_, carol_, USD, domainID, credType] = permDex;
        AMM const amm(env, alice_, XRP(10), USD(50));

        STPathSet st;
        STAmount sa, da;

        auto const& sendAmt = XRP(1);

        // doing pathfind with domain won't include amm
        std::tie(st, sa, da) =
            findPaths(env, bob_, carol_, sendAmt, std::nullopt, USD.currency, domainID);
        BEAST_EXPECT(st.empty());

        // a non-domain pathfind returns amm in the path
        std::tie(st, sa, da) = findPaths(env, bob_, carol_, sendAmt, std::nullopt, USD.currency);
        BEAST_EXPECT(same(st, stpath(gw_, ipe(xrpIssue()))));
    }

    void
    run() override
    {
        sourceCurrenciesLimit();
        noDirectPathNoIntermediaryNoAlternatives();
        directPathNoIntermediary();
        paymentAutoPathFind();
        indirectPathsPathFind();
        alternativePathsConsumeBestTransferFirst();
        issuesPathNegativeRippleClientIssue23Smaller();
        issuesPathNegativeRippleClientIssue23Larger();
        qualityPathsQualitySetAndTest();
        trustAutoClearTrustNormalClear();
        trustAutoClearTrustAutoClear();
        norippleCombinations();

        for (bool const domainEnabled : {false, true})
        {
            pathFind(domainEnabled);
            pathFindConsumeAll(domainEnabled);
            alternativePathConsumeBoth(domainEnabled);
            alternativePathsConsumeBestTransfer(domainEnabled);
            alternativePathsLimitReturnedPathsToBestQuality(domainEnabled);
            issuesPathNegativeIssue(domainEnabled);
            viaOffersViaGateway(domainEnabled);
            xrpToXrp(domainEnabled);
            receiveMax(domainEnabled);

            // The following path_find_NN tests are data driven tests
            // that were originally implemented in js/coffee and migrated
            // here. The quantities and currencies used are taken directly from
            // those legacy tests, which in some cases probably represented
            // customer use cases.

            pathFind01(domainEnabled);
            pathFind02(domainEnabled);
            pathFind04(domainEnabled);
            pathFind05(domainEnabled);
            pathFind06(domainEnabled);
        }

        hybridOfferPath();
        ammDomainPath();
    }
};

BEAST_DEFINE_TESTSUITE(Path, app, xrpl);

}  // namespace xrpl::test
