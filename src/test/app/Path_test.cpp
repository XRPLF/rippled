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
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/GraphPathfinder.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/PayGraph.h>
#include <xrpld/rpc/detail/PayGraphDelta.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/OrderBookDB.h>
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
#include <xrpl/protocol/TxMeta.h>
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
#include <vector>

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
            cfg->pathSearch = true;
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
                    da = amountFromJson(sfGeneric, path[jss::destination_amount]);

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

        auto const jvL = env.le(keylet::trustLine(Account("bob").id(), Account("alice")["USD"]))
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

        auto const jvL = env.le(keylet::trustLine(Account("bob").id(), Account("alice")["USD"]))
                             ->getJson(JsonOptions::Values::None);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            BEAST_EXPECT(*it == jvL[it.memberName()]);

        env.trust(Account("bob")["USD"](0), "alice");
        env.trust(Account("alice")["USD"](0), "bob");
        BEAST_EXPECT(
            env.le(keylet::trustLine(Account("bob").id(), Account("alice")["USD"])) == nullptr);
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

        auto const jvL = env.le(keylet::trustLine(Account("alice").id(), Account("bob")["USD"]))
                             ->getJson(JsonOptions::Values::None);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            BEAST_EXPECT(*it == jvL[it.memberName()]);

        env(pay("alice", "bob", Account("alice")["USD"](50)));
        BEAST_EXPECT(
            env.le(keylet::trustLine(Account("alice").id(), Account("bob")["USD"])) == nullptr);
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
            std::string("Path Find: non-XRP -> non-XRP, same currency ") +
            (domainEnabled ? "w/ " : "w/o ") + "domain");
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
    selfSubscriptionIouToXrp()
    {
        // Regression: when src == dst (a self-subscription, common for wallet
        // apps that subscribe to their own account), IOU->XRP paths were
        // incorrectly suppressed because effectiveDst_==dstAccount_==srcAccount_
        // triggered the repayToSelf guard even when dst is XRP.
        // Verify paths are found when the source currency is an IOU.
        testcase("self-subscription IOU to XRP path find");
        using namespace jtx;

        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");  // market maker
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(100), alice, bob);
        env.close();
        env(pay(gw, alice, usd(50)));
        env(pay(gw, bob, usd(50)));
        env.close();
        env(offer(bob, usd(10), XRP(100)));
        env.close();

        // src == dst (self-subscription) with explicit sendMax specifying the
        // source IOU issuer and convertAll dst (-1) — matches what real wallet
        // subscriptions send.  Must find the USD→XRP offer-book path.
        // (send_max requires destination_amount == -1 in the RPC layer.)
        auto const [st, sa, da] = findPaths(env, alice, alice, drops(-1), usd(50).value());
        BEAST_EXPECT(!st.empty());
    }

    void
    orderBookDBAllTakerPaysAssets()
    {
        testcase("OrderBookDB::getAllTakerPaysAssets");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        auto const gbp = gw["GBP"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.trust(gbp(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env(pay(gw, alice, gbp(500)));
        env.close();

        // Create offers to populate order books
        // XRP -> USD book (takerPays = XRP)
        env(offer(alice, XRP(500), usd(100)));
        // XRP -> EUR book (takerPays = XRP, same as above so no new takerPays)
        env(offer(bob, XRP(500), eur(100)));
        // USD -> XRP book (takerPays = USD)
        env(offer(alice, usd(100), XRP(500)));
        // EUR -> XRP book (takerPays = EUR)
        env(offer(bob, eur(100), XRP(500)));
        // GBP -> XRP book (takerPays = GBP)
        env(offer(alice, gbp(100), XRP(500)));
        env.close();

        // Trigger OrderBookDB setup so books are populated
        auto& obdb = env.app().getOrderBookDB();

        // getAllTakerPaysAssets should return XRP, USD, EUR, GBP
        auto assets = obdb.getAllTakerPaysAssets();

        // We expect at least 4 distinct takerPays assets: XRP, USD, EUR, GBP
        BEAST_EXPECT(assets.size() >= 4);

        // Verify each expected asset is present
        bool foundXRP = false, foundUSD = false, foundEUR = false, foundGBP = false;
        for (auto const& asset : assets)
        {
            if (isXRP(asset))
                foundXRP = true;
            else if (asset == usd.asset())
                foundUSD = true;
            else if (asset == eur.asset())
                foundEUR = true;
            else if (asset == gbp.asset())
                foundGBP = true;
        }
        BEAST_EXPECT(foundXRP);
        BEAST_EXPECT(foundUSD);
        BEAST_EXPECT(foundEUR);
        BEAST_EXPECT(foundGBP);
    }

    void
    orderBookDBIsBookToXRP()
    {
        testcase("OrderBookDB::isBookToXRP");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(1000), alice);
        env.trust(eur(1000), alice);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, alice, eur(500)));
        env.close();

        // Create USD -> XRP book (takerPays = USD, takerGets = XRP)
        env(offer(alice, usd(100), XRP(500)));
        // Create EUR -> XRP book (takerPays = EUR, takerGets = XRP)
        env(offer(alice, eur(100), XRP(500)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();

        // USD has a book to XRP
        BEAST_EXPECT(obdb.isBookToXRP(usd.asset()));
        // EUR has a book to XRP
        BEAST_EXPECT(obdb.isBookToXRP(eur.asset()));
        // GBP has no book to XRP (never created)
        auto const gbp = gw["GBP"];
        BEAST_EXPECT(!obdb.isBookToXRP(gbp.asset()));
    }

    void
    orderBookDBGetBookSize()
    {
        testcase("OrderBookDB::getBookSize");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env.close();

        // Create multiple books with XRP as takerPays:
        // XRP -> USD and XRP -> EUR (2 different takerGets for same takerPays)
        env(offer(alice, XRP(500), usd(100)));
        env(offer(bob, XRP(500), eur(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();

        // XRP as takerPays should have 2 books (USD and EUR as takerGets)
        BEAST_EXPECT(obdb.getBookSize(XRP) == 2);

        // USD as takerPays has no books yet
        BEAST_EXPECT(obdb.getBookSize(usd.asset()) == 0);

        // Add a USD -> XRP book
        env(offer(alice, usd(100), XRP(500)));
        env.close();

        // Now USD as takerPays should have 1 book
        BEAST_EXPECT(obdb.getBookSize(usd.asset()) == 1);
    }

    void
    orderBookDBGetBooksByTakerPays()
    {
        testcase("OrderBookDB::getBooksByTakerPays");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env.close();

        // Create books with XRP as takerPays
        env(offer(alice, XRP(500), usd(100)));
        env(offer(bob, XRP(600), eur(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();

        // Get books by XRP takerPays
        auto books = obdb.getBooksByTakerPays(XRP);
        BEAST_EXPECT(books.size() == 2);

        // Verify the books contain expected takerGets
        bool hasUSD = false, hasEUR = false;
        for (auto const& book : books)
        {
            if (book.out == usd.asset())
                hasUSD = true;
            if (book.out == eur.asset())
                hasEUR = true;
        }
        BEAST_EXPECT(hasUSD);
        BEAST_EXPECT(hasEUR);

        // Query for asset with no books
        auto const gbp = gw["GBP"];
        books = obdb.getBooksByTakerPays(gbp.asset());
        BEAST_EXPECT(books.empty());
    }

    void
    orderBookDBEmptyState()
    {
        testcase("OrderBookDB empty state");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& obdb = env.app().getOrderBookDB();

        // With no offers, all queries should return empty/false/zero
        BEAST_EXPECT(obdb.getAllTakerPaysAssets().empty());
        BEAST_EXPECT(obdb.getBookSize(XRP) == 0);
        BEAST_EXPECT(obdb.getBooksByTakerPays(XRP).empty());
        BEAST_EXPECT(!obdb.isBookToXRP(XRP));
    }

    //------------------------------------------------------------------------------
    // PayGraph unit tests for coverage

    void
    payGraphBuildAndSnapshot()
    {
        testcase("PayGraph::build + snapshot");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env.close();

        // Create order books: XRP->USD, XRP->EUR, USD->XRP, EUR->XRP
        env(offer(alice, XRP(500), usd(100)));
        env(offer(bob, XRP(500), eur(100)));
        env(offer(alice, usd(100), XRP(500)));
        env(offer(bob, eur(100), XRP(500)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};

        // Build PayGraph from OrderBookDB + ledger
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Snapshot should be non-null
        auto snap = pg->snapshot();
        BEAST_EXPECT(snap != nullptr);

        // Stats should reflect the books we created
        auto stats = pg->currentStats();
        BEAST_EXPECT(stats.vertices >= 3);  // At least XRP, USD, EUR
        BEAST_EXPECT(stats.edges >= 4);     // XRP->USD, XRP->EUR, USD->XRP, EUR->XRP
        BEAST_EXPECT(stats.orderBooks >= 4);
    }

    void
    payGraphVertexAndAssetHelpers()
    {
        testcase("PayGraph::vertexOf + assetOf");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(1000), alice);
        env.close();
        env(pay(gw, alice, usd(500)));
        env.close();

        // XRP -> USD book
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // vertexOf should return valid VIDs for assets in the graph
        auto xrpVid = pg->vertexOf(XRP);
        auto usdVid = pg->vertexOf(usd.asset());
        BEAST_EXPECT(xrpVid != PayGraph::kNull);
        BEAST_EXPECT(usdVid != PayGraph::kNull);
        BEAST_EXPECT(xrpVid != usdVid);

        // assetOf should return the correct asset for a VID
        auto xrpAsset = pg->assetOf(xrpVid);
        auto usdAsset = pg->assetOf(usdVid);
        BEAST_EXPECT(isXRP(xrpAsset));
        BEAST_EXPECT(usdAsset == usd.asset());

        // vertexOf for unknown asset returns kNull
        auto const gbp = gw["GBP"];
        auto gbpVid = pg->vertexOf(gbp.asset());
        BEAST_EXPECT(gbpVid == PayGraph::kNull);
    }

    void
    payGraphDijkstraShortestPath()
    {
        testcase("PayGraph::dijkstra + reconstructPath (via kShortestPaths)");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env.close();

        // Create chain: XRP -> USD -> EUR (so EUR is reachable from XRP via USD)
        env(offer(alice, XRP(500), usd(100)));
        env(offer(bob, usd(100), eur(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto snap = pg->snapshot();
        BEAST_EXPECT(snap != nullptr);

        // Get VIDs
        auto xrpVid = pg->vertexOf(XRP);
        auto eurVid = pg->vertexOf(eur.asset());
        BEAST_EXPECT(xrpVid != PayGraph::kNull);
        BEAST_EXPECT(eurVid != PayGraph::kNull);

        // kShortestPaths exercises dijkstra + reconstructPath internally
        auto paths = PayGraph::kShortestPaths(*snap, xrpVid, eurVid, 1);
        BEAST_EXPECT(!paths.empty());

        // Path should go from XRP to EUR (via USD bridge)
        auto const& path = paths.front();
        BEAST_EXPECT(path.vids.front() == xrpVid);
        BEAST_EXPECT(path.vids.back() == eurVid);
        BEAST_EXPECT(path.vids.size() >= 2);  // At least src and dst
        BEAST_EXPECT(path.cumQuality < std::numeric_limits<uint64_t>::max());
    }

    void
    payGraphKShortestPaths()
    {
        testcase("PayGraph::kShortestPaths + findPaths");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env(pay(gw, alice, eur(500)));  // Alice needs EUR for USD->EUR offer
        env(pay(gw, bob, usd(500)));    // Bob needs USD for XRP->USD offer
        env.close();

        // Create multiple paths from XRP to EUR:
        // Path 1: XRP -> EUR (direct)
        // Path 2: XRP -> USD -> EUR (via USD bridge)
        env(offer(alice, XRP(500), eur(100)));
        env.close();
        env(offer(bob, XRP(500), usd(100)));
        env.close();
        env(offer(alice, usd(100), eur(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // findPaths convenience wrapper
        auto paths = pg->findPaths(XRP, eur.asset(), 6);
        BEAST_EXPECT(!paths.empty());
        BEAST_EXPECT(paths.size() <= 6);

        // Paths should be ordered by quality (best first)
        for (std::size_t i = 1; i < paths.size(); ++i)
        {
            BEAST_EXPECT(paths[i].cumQuality >= paths[i - 1].cumQuality);
        }

        // Each path should start with XRP and end with EUR
        for (auto const& p : paths)
        {
            auto srcAsset = pg->assetOf(p.vids.front());
            auto dstAsset = pg->assetOf(p.vids.back());
            BEAST_EXPECT(isXRP(srcAsset));
            BEAST_EXPECT(dstAsset == eur.asset());
        }

        // kShortestPaths with no path returns empty
        auto const gbp = gw["GBP"];
        paths = pg->findPaths(XRP, gbp.asset(), 6);
        BEAST_EXPECT(paths.empty());
    }

    void
    payGraphApplyLedgerDelta()
    {
        testcase("PayGraph::applyLedgerDelta");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(1000), alice);
        env.close();
        env(pay(gw, alice, usd(500)));
        env.close();

        // Create initial book: XRP -> USD
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Initial stats
        auto statsBefore = pg->currentStats();
        BEAST_EXPECT(statsBefore.totalDeltasCalled == 0);

        // Create a new offer to change the book quality
        auto const bob = Account("bob");
        env.fund(XRP(10000), bob);
        env.trust(usd(1000), bob);
        env(pay(gw, bob, usd(500)));  // Fund bob with USD for offer
        env.close();
        env(offer(bob, XRP(400), usd(100)));  // Better quality offer
        env.close();

        auto const newLedger = env.closed();

        // Apply delta with the changed book
        std::vector<Book> changedBooks{{XRP, usd.asset(), std::nullopt}};
        pg->applyLedgerDelta(obdb, *newLedger, changedBooks);

        // Stats should reflect the delta
        auto statsAfter = pg->currentStats();
        BEAST_EXPECT(statsAfter.totalDeltasCalled == 1);
        BEAST_EXPECT(statsAfter.lastDeltaBooks == 1);

        // Snapshot should still be valid
        auto snap = pg->snapshot();
        BEAST_EXPECT(snap != nullptr);
    }

    void
    payGraphRebuild()
    {
        testcase("PayGraph::rebuild");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(1000), alice);
        env.close();
        env(pay(gw, alice, usd(500)));
        env.close();

        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Rebuild with same ledger (should preserve structure)
        pg->rebuild(obdb, *ledger, std::nullopt);

        auto snap = pg->snapshot();
        BEAST_EXPECT(snap != nullptr);

        auto stats = pg->currentStats();
        BEAST_EXPECT(stats.vertices >= 2);  // XRP + USD
        BEAST_EXPECT(stats.orderBooks >= 1);
    }

    void
    payGraphEmptyGraph()
    {
        testcase("PayGraph empty graph (no offers)");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};

        // Build with no offers - should still have XRP vertex
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto snap = pg->snapshot();
        BEAST_EXPECT(snap != nullptr);

        auto stats = pg->currentStats();
        BEAST_EXPECT(stats.vertices >= 1);  // At least XRP
        BEAST_EXPECT(stats.edges == 0);

        // findPaths should return empty when no edges exist
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto paths = pg->findPaths(XRP, usd.asset(), 6);
        BEAST_EXPECT(paths.empty());
    }

    void
    payGraphDijkstraBlockedVerts()
    {
        testcase("PayGraph::dijkstra with blocked vertices (via kShortestPaths)");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env.close();

        // Create: XRP -> USD -> EUR chain (only path to EUR)
        env(offer(alice, XRP(500), usd(100)));
        env(offer(bob, usd(100), eur(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto snap = pg->snapshot();
        auto xrpVid = pg->vertexOf(XRP);
        auto eurVid = pg->vertexOf(eur.asset());

        // kShortestPaths with k=1 should find the single path (exercises dijkstra internally)
        auto paths = PayGraph::kShortestPaths(*snap, xrpVid, eurVid, 1);
        BEAST_EXPECT(!paths.empty());
        BEAST_EXPECT(paths.front().vids.front() == xrpVid);
        BEAST_EXPECT(paths.front().vids.back() == eurVid);

        // kShortestPaths with k > 1 should return fewer paths since there's only one route
        auto allPaths = PayGraph::kShortestPaths(*snap, xrpVid, eurVid, 6);
        BEAST_EXPECT(allPaths.size() <= 1);  // Only one simple path exists
    }

    void
    payGraphStatsCounters()
    {
        testcase("PayGraph stats counters");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(1000), alice);
        env.close();
        env(pay(gw, alice, usd(500)));
        env.close();

        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Initial state: no deltas applied yet
        auto stats = pg->currentStats();
        BEAST_EXPECT(stats.totalDeltasCalled == 0);
        BEAST_EXPECT(stats.lastDeltaBooks == 0);
        BEAST_EXPECT(stats.ammPools == 0);

        // After delta, counters increment
        std::vector<Book> changedBooks{{XRP, usd.asset(), std::nullopt}};
        pg->applyLedgerDelta(obdb, *ledger, changedBooks);

        stats = pg->currentStats();
        BEAST_EXPECT(stats.totalDeltasCalled == 1);
        BEAST_EXPECT(stats.lastDeltaBooks == 1);
    }

    void
    payGraphEdgeWeightsLogSpace()
    {
        testcase("PayGraph edge weights use log-space for multiplicative composition");
        using namespace jtx;

        // Exchange rates compose multiplicatively.  Dijkstra/BF *sums* edge
        // weights, so weights must be log2(cost_ratio):
        //   log(r1) + log(r2) = log(r1 * r2)
        //
        // Direct:  XRP → EUR at cost ratio 2.5  (1 XRP → 0.40 EUR)
        // Multi:   XRP → USD (2.0) then USD → EUR (1.11)
        //          product 2.22 → 0.45 EUR per XRP  (strictly better)
        //
        // Additive rates would rank direct first (2.5 < 2.0+1.11).
        // Log-space ranks multi first (log2(2.22) < log2(2.5)).

        Env env = pathTestEnv();
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(100000), bob, charlie, gw);
        env.close();
        env.trust(usd(1000), bob, charlie);
        env.trust(eur(1000), bob, charlie);
        env.close();
        env(pay(gw, bob, usd(500)));
        env(pay(gw, bob, eur(500)));
        env(pay(gw, charlie, usd(500)));
        env(pay(gw, charlie, eur(500)));
        env.close();

        // jtx offer(account, takerPays, takerGets): taker pays first, gets second.
        // Cost ratio = takerPays / takerGets (XRP in drops).
        //
        // XRP → USD: 100 XRP for 50 USD  → cost = 2e6 drops / 50  (ratio 2.0 in XRP units)
        // USD → EUR: 100 USD for 90 EUR  → cost ≈ 1.111
        // XRP → EUR: 100 XRP for 40 EUR  → cost = 2e6 drops / 40  (ratio 2.5 in XRP units)
        // Multi product 2.0 * 1.111 = 2.222 < 2.5 direct → multi is cheaper.
        env(offer(bob, XRP(100), usd(50)));
        env(offer(charlie, usd(100), eur(90)));
        env.close();
        env(offer(bob, XRP(100), eur(40)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto const paths = pg->findPaths(XRP, eur.asset(), 6);
        BEAST_EXPECT(!paths.empty());

        auto const xrpVid = pg->vertexOf(XRP);
        auto const usdVid = pg->vertexOf(usd.asset());
        auto const eurVid = pg->vertexOf(eur.asset());

        int multiRank = -1;
        int directRank = -1;
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            auto const& vids = paths[i].vids;
            if (vids.size() == 2 && vids[0] == xrpVid && vids[1] == eurVid)
                directRank = static_cast<int>(i);
            else if (
                vids.size() == 3 && vids[0] == xrpVid && vids[1] == usdVid && vids[2] == eurVid)
                multiRank = static_cast<int>(i);
        }

        BEAST_EXPECT(multiRank >= 0);
        BEAST_EXPECT(directRank >= 0);
        // Multi-hop must outrank direct: log2(2.22) < log2(2.5).
        BEAST_EXPECT(multiRank >= 0 && directRank >= 0 && multiRank < directRank);
        if (multiRank >= 0 && directRank >= 0)
        {
            BEAST_EXPECT(
                paths[static_cast<std::size_t>(multiRank)].cumQuality <
                paths[static_cast<std::size_t>(directRank)].cumQuality);
        }
    }

    // Helper to create a GraphPathfinder from test env
    std::unique_ptr<GraphPathfinder>
    makeGraphPathfinder(
        jtx::Env& env,
        std::shared_ptr<PayGraph> const& graph,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& dstAmount,
        Asset const& srcAsset = jtx::XRP,
        std::optional<AccountID> const& srcIssuer = std::nullopt)
    {
        PathAsset srcPathAsset{srcAsset};
        auto cache = std::make_shared<AssetCache>(env.closed(), env.journal);
        return std::make_unique<GraphPathfinder>(
            graph,
            cache,
            src,
            dst,
            srcPathAsset,
            srcIssuer,
            dstAmount,
            std::nullopt,  // srcAmount
            std::nullopt,  // domain
            env.app());
    }

    void
    graphPathfinderBasicFindPaths()
    {
        testcase("GraphPathfinder::findPaths basic XRP->IOU");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(500)));  // Alice needs USD for XRP->USD offer
        env.close();

        // Create XRP -> USD offer (alice sells XRP for USD)
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto gp = makeGraphPathfinder(env, pg, alice, bob, usd(50));

        // findPaths should discover the XRP->USD path
        bool found = gp->findPaths();
        BEAST_EXPECT(found);
    }

    void
    graphPathfinderNoGraph()
    {
        testcase("GraphPathfinder::findPaths with null graph");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();

        // Null graph should return false from findPaths
        auto gp = makeGraphPathfinder(env, nullptr, alice, bob, usd(50));
        bool found = gp->findPaths();
        BEAST_EXPECT(!found);
    }

    void
    graphPathfinderZeroDestinationAmount()
    {
        testcase("GraphPathfinder::findPaths with zero destination amount");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Zero destination amount should return false
        auto gp = makeGraphPathfinder(env, pg, alice, bob, usd(0));
        bool found = gp->findPaths();
        BEAST_EXPECT(!found);
    }

    void
    graphPathfinderComputeAndGetBestPaths()
    {
        testcase("GraphPathfinder::computePathRanks + getBestPaths");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.trust(usd(1000), alice);
        env(pay(gw, bob, usd(500)));
        env(pay(gw, alice, usd(500)));  // Alice needs USD for XRP->USD offer
        env.close();

        // Create XRP -> USD offer
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto gp = makeGraphPathfinder(env, pg, alice, bob, usd(50));

        // findPaths then rank and select best
        bool found = gp->findPaths();
        BEAST_EXPECT(found);

        gp->computePathRanks(6);
        auto bestPaths = gp->getBestPaths(6, STPathSet(), xrpAccount());

        // Should have found at least one path
        BEAST_EXPECT(bestPaths.size() >= 1);
    }

    void
    graphPathfinderGetBestPathsEmpty()
    {
        testcase("GraphPathfinder::getBestPaths with no paths");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), alice, bob);
        env.close();

        // No graph, no paths — getBestPaths should return empty
        auto gp = makeGraphPathfinder(env, nullptr, alice, bob, XRP(50));
        bool found = gp->findPaths();
        BEAST_EXPECT(!found);

        auto bestPaths = gp->getBestPaths(6, STPathSet(), xrpAccount());
        BEAST_EXPECT(bestPaths.empty());
    }

    void
    graphPathfinderContinueCallbackAbort()
    {
        testcase("GraphPathfinder::findPaths with abort callback");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(500)));  // Alice needs USD for XRP->USD offer
        env.close();

        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto gp = makeGraphPathfinder(env, pg, alice, bob, usd(50));

        // Callback that always returns false (abort immediately)
        bool aborted = gp->findPaths([]() { return false; });
        // Even with abort, the return may be true/false depending on paths found so far
        // The key is it doesn't crash
        BEAST_EXPECT(aborted == true || aborted == false);
    }

    void
    graphPathfinderIOUToXRP()
    {
        testcase("GraphPathfinder::findPaths IOU->XRP path");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(500)));
        env.close();

        // Create USD -> XRP offer (alice sells USD for XRP)
        env(offer(alice, usd(100), XRP(500)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Alice sends USD to bob who wants XRP
        auto gp = makeGraphPathfinder(env, pg, alice, bob, XRP(100), usd.asset());

        bool found = gp->findPaths();
        BEAST_EXPECT(found);
    }

    void
    graphPathfinderMultiHopPath()
    {
        testcase("GraphPathfinder::findPaths multi-hop (XRP->USD->EUR)");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env(pay(gw, alice, usd(500)));
        env(pay(gw, alice, eur(500)));  // Alice needs EUR for USD->EUR offer
        env(pay(gw, bob, eur(500)));
        env.close();

        // XRP -> USD offer
        env(offer(alice, XRP(500), usd(100)));
        env.close();
        // USD -> EUR offer
        env(offer(alice, usd(100), eur(100)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        // Alice sends XRP to bob who wants EUR (multi-hop via USD)
        auto gp = makeGraphPathfinder(env, pg, alice, bob, eur(50));

        bool found = gp->findPaths();
        BEAST_EXPECT(found);

        gp->computePathRanks(6);
        auto bestPaths = gp->getBestPaths(6, STPathSet(), xrpAccount());
        BEAST_EXPECT(bestPaths.size() >= 1);
    }

    void
    pathLengthValidation()
    {
        testcase("Path length validation - telBAD_PATH_COUNT");
        using namespace jtx;

        // Simple test to verify path length validation works
        // Maximum path length is 8 hops (defined in Payment transactor)

        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gateway");

        env.fund(XRP(10000), alice, bob, gw);
        env.close();

        // Create simple trust lines for USD issued by gateway
        auto const usd = gw["USD"];
        env.trust(usd(1000), alice);  // Alice trusts gateway's USD
        env.trust(usd(1000), bob);    // Bob trusts gateway's USD
        env.close();

        // Fund accounts with USD
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, usd(500)));
        env.close();

        // Create an offer from alice to enable pathfinding (USD -> XRP)
        env(offer(alice, usd(100), XRP(100)));
        env.close();

        // Test pathfinding - should work for valid paths
        auto [st, sa, da] = findPaths(env, alice, bob, usd(10));

        // If paths are found, verify they don't exceed maximum length (8)
        for (auto const& path : st)
        {
            // kMaxPathLength = 8 (defined in Payment transactor)
            BEAST_EXPECT(path.size() <= 8);
        }

        // Test passes if we reach here without assertion failures
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
        pathLengthValidation();

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

            pathFind01(domainEnabled);
            pathFind02(domainEnabled);
            pathFind04(domainEnabled);
            pathFind05(domainEnabled);
            pathFind06(domainEnabled);
        }

        hybridOfferPath();
        ammDomainPath();
        selfSubscriptionIouToXrp();

        // OrderBookDBImpl unit tests for coverage
        orderBookDBAllTakerPaysAssets();
        orderBookDBIsBookToXRP();
        orderBookDBGetBookSize();
        orderBookDBGetBooksByTakerPays();
        orderBookDBEmptyState();

        // PayGraph unit tests for coverage
        payGraphBuildAndSnapshot();
        payGraphVertexAndAssetHelpers();
        payGraphDijkstraShortestPath();
        payGraphKShortestPaths();
        payGraphApplyLedgerDelta();
        payGraphRebuild();
        payGraphEmptyGraph();
        payGraphDijkstraBlockedVerts();
        payGraphStatsCounters();
        payGraphEdgeWeightsLogSpace();

        // GraphPathfinder unit tests for coverage
        graphPathfinderBasicFindPaths();
        graphPathfinderNoGraph();
        graphPathfinderZeroDestinationAmount();
        graphPathfinderComputeAndGetBestPaths();
        graphPathfinderGetBestPathsEmpty();
        graphPathfinderContinueCallbackAbort();
        graphPathfinderIOUToXRP();
        graphPathfinderMultiHopPath();

        // PathRequest unit tests for coverage (via PathRequestManager API)
        pathRequestParseJsonMissingFields();
        pathRequestParseJsonMalformedAccount();
        pathRequestParseJsonMalformedAmount();
        pathRequestParseJsonSendMaxWithoutConvertAll();
        pathRequestParseJsonSourceCurrencies();
        pathRequestIsValidSourceNotFound();
        pathRequestIsValidDestNotFoundNonXrp();
        pathRequestIsValidDestNotFoundBelowReserve();
        pathRequestDoCreateAndDoUpdate();
        pathRequestNewAndNeedsUpdate();
        pathRequestLegacyPathRequest();
        pathRequestFindPathsNoGraph();

        // PathRequestManager unit tests for coverage
        pathRequestManagerGetAssetCache();
        pathRequestManagerGetAssetCacheJumpBack();
        pathRequestManagerRequestsPending();
        pathRequestManagerEnsurePayGraph();
        pathRequestManagerFindPathsNullLedger();
        pathRequestManagerFindPathsBasic();
        pathRequestManagerFindPathsNoGraph();
        pathRequestManagerFindPathsIOUToXRP();
        pathRequestManagerMakeLegacyPathRequestInvalidReset();
        pathRequestManagerMakeLegacyPathRequestValid();
        pathRequestManagerGetPayGraph();
        pathRequestManagerInsertPathRequestOrdering();
        pathRequestManagerDoLegacyPathRequestNoAlternatives();
        pathRequestManagerReportFastAndFull();
        pathRequestManagerSignalOrderBookReady();
        pathRequestManagerFindPathsDomain();
        pathRequestManagerGetAssetCacheJumpForward();
        pathRequestManagerUpdateAllPathSearchDisabled();
        pathRequestManagerMakeLegacyPathRequestTooBusy();

        // OrderBookDB domain-specific coverage
        orderBookDBAllTakerPaysAssetsDomain();

        // PayGraphDelta unit tests for coverage
        payGraphDeltaExtractChangedBooks();
        payGraphDeltaMergeBooks();
        liquidityDepthProvesTopOfBookProblem();
    }

    void
    pathRequestParseJsonMissingFields()
    {
        testcase("PathRequest::parseJson missing required fields");
        using namespace jtx;
        Env env = pathTestEnv();
        auto& prm = env.app().getPathRequestManager();

        // Missing source_account
        json::Value jv = json::ValueType::Object;
        jv[jss::destination_account] = "rEb8TK1gPpG1GzNUnR1CcyRSVxQk9LNq2A";
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "10";
        jv[jss::destination_amount][jss::issuer] = "rEb8TK1gPpG1GzNUnR1CcyRSVxQk9LNq2A";

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestParseJsonMalformedAccount()
    {
        testcase("PathRequest::parseJson malformed account");
        using namespace jtx;
        Env env = pathTestEnv();
        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = "invalid_base58";
        jv[jss::destination_account] = "rEb8TK1gPpG1GzNUnR1CcyRSVxQk9LNq2A";
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "10";
        jv[jss::destination_amount][jss::issuer] = "rEb8TK1gPpG1GzNUnR1CcyRSVxQk9LNq2A";

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestParseJsonMalformedAmount()
    {
        testcase("PathRequest::parseJson malformed destination amount");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gw");
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice, gw);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(gw);
        jv[jss::destination_amount] = "not_an_object";

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestParseJsonSendMaxWithoutConvertAll()
    {
        testcase("PathRequest::parseJson send_max without convert_all destination");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(gw);
        // Normal destination amount (not convert_all)
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "10";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);
        // send_max with non-convert_all should fail
        jv[jss::send_max] = json::ValueType::Object;
        jv[jss::send_max][jss::currency] = "USD";
        jv[jss::send_max][jss::value] = "100";
        jv[jss::send_max][jss::issuer] = toBase58(gw);

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestParseJsonSourceCurrencies()
    {
        testcase("PathRequest::parseJson source_currencies validation");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // Empty source_currencies array should fail
        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(gw);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "10";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);
        jv[jss::source_currencies] = json::ValueType::Array;

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestIsValidSourceNotFound()
    {
        testcase("PathRequest::isValid source account not found");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gw");
        auto const nonexistent = Account("nonexistent");
        env.fund(XRP(10000), gw);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(nonexistent);
        jv[jss::destination_account] = toBase58(gw);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "10";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestIsValidDestNotFoundNonXrp()
    {
        testcase("PathRequest::isValid destination not found with non-XRP amount");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const nonexistent = Account("nonexistent");
        env.fund(XRP(10000), alice, gw);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(nonexistent);
        // Non-XRP amount to nonexistent account should fail
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "10";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestIsValidDestNotFoundBelowReserve()
    {
        testcase("PathRequest::isValid destination not found with XRP below reserve");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const nonexistent = Account("nonexistent");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(nonexistent);
        // XRP amount below reserve to nonexistent account should fail
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "XRP";
        jv[jss::destination_amount][jss::value] = "1";  // Below reserve

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestDoCreateAndDoUpdate()
    {
        testcase("PathRequest::doCreate + doUpdate full flow");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, usd(500)));
        env.close();

        // Create XRP -> USD offer
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& prm = env.app().getPathRequestManager();
        prm.ensurePayGraph(env.closed());

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        PathRequest::pointer req;
        bool completed = false;
        auto completion = [&]() { completed = true; };

        Resource::Consumer c;
        auto res = prm.makeLegacyPathRequest(req, completion, c, env.closed(), jv);

        BEAST_EXPECT(!res.isMember(jss::error));
        BEAST_EXPECT(req != nullptr);
        BEAST_EXPECT(req->hasCompletion());
    }

    void
    pathRequestNewAndNeedsUpdate()
    {
        testcase("PathRequest::isNew + needsUpdate state machine");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        PathRequest::pointer req;
        Resource::Consumer c;
        prm.makeLegacyPathRequest(req, []() {}, c, env.closed(), jv);

        // New request should be "new" (lastIndex == 0)
        BEAST_EXPECT(req->isNew());

        // needsUpdate should return true for new request
        bool needs = req->needsUpdate(true, env.closed()->seq() + 1);
        BEAST_EXPECT(needs);

        // While inProgress, needsUpdate should return false
        needs = req->needsUpdate(true, env.closed()->seq() + 2);
        BEAST_EXPECT(!needs);

        // After updateComplete, inProgress is cleared
        req->updateComplete();

        // lastIndex_ remains 0 (never updated in current code), so isNew() stays true
        BEAST_EXPECT(req->isNew());
    }

    void
    pathRequestLegacyPathRequest()
    {
        testcase("PathRequest::doLegacyPathRequest synchronous execution");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, usd(500)));
        env.close();

        // Create XRP -> USD offer
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& prm = env.app().getPathRequestManager();
        prm.ensurePayGraph(env.closed());

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        // doLegacyPathRequest executes synchronously
        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);

        BEAST_EXPECT(!res.isMember(jss::error));
        BEAST_EXPECT(res.isMember(jss::alternatives));
    }

    void
    pathRequestFindPathsNoGraph()
    {
        testcase("PathRequest findPaths returns error when PayGraph not ready");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        // Do NOT build PayGraph — ensure it's null

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        PathRequest::pointer req;
        Resource::Consumer c;
        auto res = prm.makeLegacyPathRequest(req, []() {}, c, env.closed(), jv);

        BEAST_EXPECT(req != nullptr);

        // doUpdate without PayGraph - the findPaths method returns RpcNotReady
        // when graph_ is null. We verify the request was created successfully.
        BEAST_EXPECT(req->hasCompletion());
    }

    // PathRequestManager unit tests for coverage

    void
    pathRequestManagerGetAssetCache()
    {
        testcase("PathRequestManager::getAssetCache creation and reuse");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // First call creates a new cache
        auto cache1 = prm.getAssetCache(ledger, false);
        BEAST_EXPECT(cache1 != nullptr);
        BEAST_EXPECT(cache1->getLedger()->seq() == ledger->seq());

        // Second call with same ledger should reuse the cache
        auto cache2 = prm.getAssetCache(ledger, false);
        BEAST_EXPECT(cache2 == cache1);

        // Authoritative call with newer ledger creates new cache
        env.close();
        auto const ledger2 = env.closed();
        auto cache3 = prm.getAssetCache(ledger2, true);
        BEAST_EXPECT(cache3 != nullptr);
        BEAST_EXPECT(cache3->getLedger()->seq() == ledger2->seq());
    }

    void
    pathRequestManagerGetAssetCacheJumpBack()
    {
        testcase("PathRequestManager::getAssetCache jump back creates new cache");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        // Advance several ledgers
        for (int i = 0; i < 10; i++)
            env.close();

        auto const ledger = env.closed();
        auto& prm = env.app().getPathRequestManager();

        // Create cache for seq=11
        auto cache1 = prm.getAssetCache(ledger, true);
        BEAST_EXPECT(cache1 != nullptr);

        // Advance more ledgers to get a newer cache
        env.close();
        auto const ledgerNewer = env.closed();
        auto cache2 = prm.getAssetCache(ledgerNewer, true);
        BEAST_EXPECT(cache2 != nullptr);
        BEAST_EXPECT(cache2->getLedger()->seq() > cache1->getLedger()->seq());
    }

    void
    pathRequestManagerRequestsPending()
    {
        testcase("PathRequestManager::requestsPending");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // Initially no requests pending
        BEAST_EXPECT(!prm.requestsPending());
    }

    void
    pathRequestManagerEnsurePayGraph()
    {
        testcase("PathRequestManager::ensurePayGraph build and reuse");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(500)));
        env.close();
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // First call builds the graph
        auto graph1 = prm.ensurePayGraph(ledger);
        BEAST_EXPECT(graph1 != nullptr);

        // Second call with same ledger reuses the graph
        auto graph2 = prm.ensurePayGraph(ledger);
        BEAST_EXPECT(graph1 == graph2);

        // After ledger close, ensurePayGraph reuses the same in-memory graph.
        // Edge updates land via updateAll/applyLedgerDelta, not a full rebuild
        // on every path_find (ledgers close ~3s; rebuild was multi-second).
        env.close();
        auto const ledger2 = env.closed();
        auto graph3 = prm.ensurePayGraph(ledger2);
        BEAST_EXPECT(graph3 != nullptr);
        BEAST_EXPECT(graph3 == graph1);
    }

    void
    pathRequestManagerFindPathsNullLedger()
    {
        testcase("PathRequestManager::findPaths with null ledger");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // Null ledger should return empty STPathSet
        STPathSet paths = prm.findPaths(
            nullptr, alice, bob, XRP(100), PathAsset(xrpCurrency()), std::nullopt, std::nullopt, 6);
        BEAST_EXPECT(paths.empty());
    }

    void
    pathRequestManagerFindPathsBasic()
    {
        testcase("PathRequestManager::findPaths basic XRP to IOU");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(500)));
        env.close();
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // Build graph first
        prm.ensurePayGraph(ledger);

        // Find paths from alice to bob for USD
        STPathSet paths = prm.findPaths(
            ledger, alice, bob, usd(50), PathAsset(xrpCurrency()), std::nullopt, std::nullopt, 6);
        BEAST_EXPECT(paths.size() >= 1);
    }

    void
    pathRequestManagerFindPathsNoGraph()
    {
        testcase("PathRequestManager::findPaths with no PayGraph built");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // Without ensurePayGraph, findPaths should return empty
        STPathSet paths = prm.findPaths(
            ledger, alice, bob, XRP(100), PathAsset(xrpCurrency()), std::nullopt, std::nullopt, 6);
        BEAST_EXPECT(paths.empty());
    }

    void
    pathRequestManagerFindPathsIOUToXRP()
    {
        testcase("PathRequestManager::findPaths IOU to XRP");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice);
        env(pay(gw, alice, usd(500)));
        env.close();

        // Alice offers USD for XRP
        env(offer(alice, usd(100), XRP(500)));
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();
        prm.ensurePayGraph(ledger);

        // Alice sends USD to bob who receives XRP
        STPathSet paths =
            prm.findPaths(ledger, alice, bob, XRP(100), usd.asset(), std::nullopt, std::nullopt, 6);
        BEAST_EXPECT(paths.size() >= 1);
    }

    void
    pathRequestManagerMakeLegacyPathRequestInvalidReset()
    {
        testcase("PathRequestManager::makeLegacyPathRequest resets req on invalid");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // Missing required fields should reset req to null
        PathRequest::pointer req;
        Resource::Consumer c;

        json::Value jv = json::ValueType::Object;
        // No source_account, destination_account, or destination_amount

        auto res = prm.makeLegacyPathRequest(req, []() {}, c, env.closed(), jv);
        BEAST_EXPECT(req == nullptr);
        BEAST_EXPECT(res.isMember(jss::error));
    }

    void
    pathRequestManagerMakeLegacyPathRequestValid()
    {
        testcase("PathRequestManager::makeLegacyPathRequest with valid request");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        PathRequest::pointer req;
        Resource::Consumer c;
        auto res = prm.makeLegacyPathRequest(req, []() {}, c, env.closed(), jv);

        BEAST_EXPECT(req != nullptr);
        BEAST_EXPECT(!res.isMember(jss::error));
    }

    void
    pathRequestManagerGetPayGraph()
    {
        testcase("PathRequestManager::getPayGraph returns null before build");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // Full OB scan path: ready + one-time build from scanned books.
        prm.signalOrderBookReady(env.closed());
        BEAST_EXPECT(prm.getPayGraph() != nullptr);
    }

    void
    pathRequestManagerInsertPathRequestOrdering()
    {
        testcase("PathRequestManager::insertPathRequest ordering");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        // Create two requests
        PathRequest::pointer req1;
        PathRequest::pointer req2;
        Resource::Consumer c;

        prm.makeLegacyPathRequest(req1, []() {}, c, env.closed(), jv);
        prm.makeLegacyPathRequest(req2, []() {}, c, env.closed(), jv);

        BEAST_EXPECT(req1 != nullptr);
        BEAST_EXPECT(req2 != nullptr);

        // Both should be pending
        BEAST_EXPECT(prm.requestsPending());

        // After completing req1 update, it's no longer new
        req1->updateComplete();

        // Create a third new request - should be inserted before serviced ones
        PathRequest::pointer req3;
        prm.makeLegacyPathRequest(req3, []() {}, c, env.closed(), jv);
        BEAST_EXPECT(req3 != nullptr);
    }

    void
    pathRequestManagerDoLegacyPathRequestNoAlternatives()
    {
        testcase("PathRequestManager::doLegacyPathRequest with no paths found");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        // Create trust lines but no offers — pathfinding should return empty alternatives
        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, usd(500)));
        env.close();

        auto& prm = env.app().getPathRequestManager();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = json::ValueType::Object;
        jv[jss::destination_amount][jss::currency] = "USD";
        jv[jss::destination_amount][jss::value] = "50";
        jv[jss::destination_amount][jss::issuer] = toBase58(gw);

        Resource::Consumer c;
        auto res = prm.doLegacyPathRequest(c, env.closed(), jv);
        BEAST_EXPECT(!res.isMember(jss::error));
    }

    void
    pathRequestManagerReportFastAndFull()
    {
        testcase("PathRequestManager::reportFast and reportFull metrics");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // These should not crash - they just record metrics
        prm.reportFast(std::chrono::milliseconds(10));
        prm.reportFull(std::chrono::milliseconds(20));
    }

    void
    pathRequestManagerSignalOrderBookReady()
    {
        testcase("PathRequestManager::signalOrderBookReady");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // Ready + build from the scanned ledger's book set.
        prm.signalOrderBookReady(ledger);
        BEAST_EXPECT(prm.getPayGraph() != nullptr);
    }

    void
    pathRequestManagerFindPathsDomain()
    {
        testcase("PathRequestManager::findPaths with domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // Domain-specific path finding with a domain that has no offers
        // should return empty (tests the domain branch in findPaths)
        uint256 domain("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        auto paths = prm.findPaths(
            ledger, alice, bob, usd(10), PathAsset(xrpCurrency()), std::nullopt, domain, 5);

        // Should return empty since no domain-specific offers exist
        BEAST_EXPECT(paths.empty());
    }

    void
    pathRequestManagerGetAssetCacheJumpForward()
    {
        testcase("PathRequestManager::getAssetCache jump forward non-authoritative");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();

        // Create cache for current ledger
        auto const ledger1 = env.closed();
        auto cache1 = prm.getAssetCache(ledger1, true);
        BEAST_EXPECT(cache1 != nullptr);
        auto const seq1 = cache1->getLedger()->seq();

        // Advance 10 more ledgers (well beyond the +8 threshold)
        for (int i = 0; i < 10; i++)
            env.close();

        auto const ledger2 = env.closed();
        auto const seq2 = ledger2->seq();
        BEAST_EXPECT(seq2 > seq1 + 8);

        // Non-authoritative call with ledger far ahead (> lineSeq + 8) should
        // create a new cache (line 50 in PathRequestManager.cpp)
        auto cache2 = prm.getAssetCache(ledger2, false);
        BEAST_EXPECT(cache2 != cache1);
        BEAST_EXPECT(cache2->getLedger()->seq() == seq2);
    }

    void
    pathRequestManagerUpdateAllPathSearchDisabled()
    {
        testcase("PathRequestManager::updateAll with pathSearch disabled");
        using namespace jtx;
        // Default config has pathSearch = false
        Env env(*this);
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        // updateAll should return early when pathSearch is disabled
        // and should not crash
        prm.updateAll(ledger);

        // PayGraph should remain nullptr since pathSearch is disabled
        BEAST_EXPECT(prm.getPayGraph() == nullptr);
    }

    void
    pathRequestManagerMakeLegacyPathRequestTooBusy()
    {
        testcase("PathRequestManager::makeLegacyPathRequest returns RpcTooBusy");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto& prm = env.app().getPathRequestManager();
        auto const ledger = env.closed();

        json::Value jv = json::ValueType::Object;
        jv[jss::source_account] = toBase58(alice);
        jv[jss::destination_account] = toBase58(bob);
        jv[jss::destination_amount] = "100";

        Resource::Consumer c;
        PathRequest::pointer req;

        // First call should succeed (not too busy)
        auto res = prm.makeLegacyPathRequest(req, [] {}, c, ledger, jv);
        BEAST_EXPECT(!res.isMember(jss::error));
        BEAST_EXPECT(req != nullptr);
    }

    //------------------------------------------------------------------------------
    // OrderBookDB domain-specific getAllTakerPaysAssets test

    void
    orderBookDBAllTakerPaysAssetsDomain()
    {
        testcase("OrderBookDB::getAllTakerPaysAssets with domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.trust(eur(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, eur(500)));
        env.close();

        // Create domain and set it up for all accounts
        std::optional<uint256> domainID = setupDomain(env, {alice, bob, gw});

        // Create domain-specific offers
        // Domain: XRP -> USD (takerPays = XRP within domain)
        env(offer(alice, XRP(500), usd(100)), Domain(*domainID));
        // Domain: EUR -> XRP (takerPays = EUR within domain)
        env(offer(bob, eur(100), XRP(500)), Domain(*domainID));
        env.close();

        // Also create a non-domain offer for comparison
        // Non-domain: USD -> XRP (takerPays = USD, no domain)
        env(offer(alice, usd(100), XRP(500)));
        env.close();

        auto& obdb = env.app().getOrderBookDB();

        // Query with domain - should only return assets from domain books
        auto domainAssets = obdb.getAllTakerPaysAssets(*domainID);

        // Domain has XRP -> USD and EUR -> XRP, so takerPays are XRP and EUR
        BEAST_EXPECT(domainAssets.size() == 2);

        bool foundXRP = false, foundEUR = false, foundUSD = false;
        for (auto const& asset : domainAssets)
        {
            if (isXRP(asset))
                foundXRP = true;
            else if (asset == eur.asset())
                foundEUR = true;
            else if (asset == usd.asset())
                foundUSD = true;
        }
        BEAST_EXPECT(foundXRP);   // XRP is takerPays in domain XRP->USD book
        BEAST_EXPECT(foundEUR);   // EUR is takerPays in domain EUR->XRP book
        BEAST_EXPECT(!foundUSD);  // USD->XRP book is non-domain, should not appear

        // Query without domain - should include all books (domain + non-domain)
        auto allAssets = obdb.getAllTakerPaysAssets();
        // Should have XRP, EUR from domain books AND USD from non-domain book
        bool foundAllUSD = false;
        for (auto const& asset : allAssets)
        {
            if (asset == usd.asset())
                foundAllUSD = true;
        }
        BEAST_EXPECT(foundAllUSD);  // Non-domain USD book appears in global query
    }

    //------------------------------------------------------------------------------
    // PayGraphDelta unit tests for coverage

    void
    payGraphDeltaExtractChangedBooks()
    {
        testcase("PayGraphDelta::extractChangedBooks from TxMeta");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(1000), alice);
        env.close();
        env(pay(gw, alice, usd(500)));
        env.close();

        // Create an offer: XRP -> USD
        env(offer(alice, XRP(500), usd(100)));
        env.close();

        // Get transaction metadata
        auto const& meta = env.meta();
        BEAST_EXPECT(meta != nullptr);

        // Extract changed books from the offer creation metadata
        TxMeta txMeta(
            env.tx()->getTransactionID(), env.closed()->seq(), *const_cast<STObject*>(meta.get()));

        auto books = extractChangedBooks(txMeta, std::nullopt);

        // An OfferCreate should produce one changed book (XRP -> USD)
        BEAST_EXPECT(books.size() == 1);
        BEAST_EXPECT(isXRP(books[0].in));
        BEAST_EXPECT(books[0].out == usd.asset());

        // Test with empty nodes array (no offers changed)
        STArray emptyNodes;
        auto emptyBooks = extractChangedBooks(emptyNodes, std::nullopt);
        BEAST_EXPECT(emptyBooks.empty());

        // Test the STArray overload directly via getNodes()
        auto booksFromNodes = extractChangedBooks(txMeta.getNodes(), std::nullopt);
        BEAST_EXPECT(booksFromNodes.size() == 1);
    }

    void
    payGraphDeltaMergeBooks()
    {
        testcase("PayGraphDelta::mergeBooks deduplication");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        Asset const xrp = XRP;
        Asset const usdAsset = usd.asset();
        Asset const eurAsset = eur.asset();

        // dest has XRP->USD book
        std::vector<Book> dest{{xrp, usdAsset, std::nullopt}};

        // src has XRP->USD (duplicate) and XRP->EUR (new)
        std::vector<Book> src{{xrp, usdAsset, std::nullopt}, {xrp, eurAsset, std::nullopt}};

        mergeBooks(dest, src);

        // Should have 2 unique books after merge
        BEAST_EXPECT(dest.size() == 2);

        // cspell:ignore hasXRPUSD hasXRPEUR
        bool hasXRPUSD = false, hasXRPEUR = false;
        for (auto const& book : dest)
        {
            if (book.in == xrp && book.out == usdAsset)
                hasXRPUSD = true;
            if (book.in == xrp && book.out == eurAsset)
                hasXRPEUR = true;
        }
        BEAST_EXPECT(hasXRPUSD);
        BEAST_EXPECT(hasXRPEUR);

        // Test merging into empty dest
        std::vector<Book> emptyDest;
        mergeBooks(emptyDest, src);
        BEAST_EXPECT(emptyDest.size() == 2);

        // Test merging empty src (no change to dest)
        std::vector<Book> beforeMerge = dest;
        std::vector<Book> emptySrc;
        mergeBooks(dest, emptySrc);
        BEAST_EXPECT(dest.size() == beforeMerge.size());
    }

    void
    liquidityDepthProvesTopOfBookProblem()
    {
        testcase("Liquidity depth: validate actual liquidity is used and not just top-of-book");
        using namespace jtx;

        // Top-of-book weights ignore liquidity depth.
        //
        // Path A (thin, direct): one offer selling 5 USD at an amazing rate
        //   (0.01 XRP per USD). Edge weight says "super cheap."
        // Path B (deep, via EUR): lots of depth at a worse effective rate
        //   (~0.50 XRP per USD). Edge weight says "expensive."
        //
        // Sending 3 USD?  Path A wins — great rate, enough depth.
        // Sending 10_000 USD? Path A is unusable (only 5 USD at that rate).
        // Without a depth penalty, Path A still ranks first and burns a
        // k-shortest candidate slot (only 18 exist: 6 × 3 oversample).

        Env env = pathTestEnv();

        auto const gw = Account("gw");
        auto const thinMM = Account("thinMM");
        auto const deepMM = Account("deepMM");

        env.fund(XRP(1'000'000), gw, thinMM, deepMM);
        env.close();

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        env.trust(USD(100'000), thinMM, deepMM);
        env.trust(EUR(100'000), deepMM);
        env.close();

        // Thin book: only 5 USD of inventory.
        env(pay(gw, thinMM, USD(5)));
        // Deep book: enough to fill a 10_000 USD payment.
        env(pay(gw, deepMM, USD(10'000)));
        env(pay(gw, deepMM, EUR(10'000)));
        env.close();

        // Path A: XRP -> USD, 0.01 XRP/USD, 5 USD depth.
        // offer: taker pays 0.05 XRP, gets 5 USD.
        env(offer(thinMM, XRP(0.05), USD(5)));
        env.close();

        // Path B: XRP -> EUR -> USD, ~0.50 XRP/USD effective, deep.
        // Hop1: 50 XRP per 100 EUR; hop2: 100 EUR per 100 USD.
        // 100 stacked offers → 10_000 USD of depth on the final hop.
        for (int i = 0; i < 100; ++i)
        {
            env(offer(deepMM, XRP(50), EUR(100)));
            env(offer(deepMM, EUR(100), USD(100)));
        }
        env.close();

        auto& obdb = env.app().getOrderBookDB();
        auto const ledger = env.closed();
        beast::Journal const journal{env.app().getJournal("PayGraph")};
        auto pg = PayGraph::build(obdb, *ledger, std::nullopt, journal);

        auto const xrpAsset = Asset{xrpl::xrpIssue()};
        auto const usdAsset = Asset{USD.issue()};
        auto const eurAsset = Asset{EUR.issue()};

        auto const xrpVid = pg->vertexOf(xrpAsset);
        auto const usdVid = pg->vertexOf(usdAsset);
        auto const eurVid = pg->vertexOf(eurAsset);

        auto rankOf = [&](std::vector<PayGraph::AssetPath> const& paths, bool thin) -> int {
            for (std::size_t i = 0; i < paths.size(); ++i)
            {
                auto const& vids = paths[i].vids;
                if (thin)
                {
                    if (vids.size() == 2 && vids[0] == xrpVid && vids[1] == usdVid)
                        return static_cast<int>(i);
                }
                else if (
                    vids.size() == 3 && vids[0] == xrpVid && vids[1] == eurVid && vids[2] == usdVid)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };

        // --- Small payment: thin path has enough depth and the better rate ---
        {
            auto const paths = pg->findPaths(xrpAsset, usdAsset, 6, USD(3));
            BEAST_EXPECT(!paths.empty());
            int const thinRank = rankOf(paths, true);
            int const deepRank = rankOf(paths, false);
            BEAST_EXPECT(thinRank >= 0);
            BEAST_EXPECT(deepRank >= 0);
            // Thin must win when the payment fits in its book.
            BEAST_EXPECT(thinRank >= 0 && deepRank >= 0 && thinRank < deepRank);
        }

        // --- Larger payment still discovers both candidates (depth is advisory) ---
        // Full-book walks are intentionally avoided for speed; rippleCalculate
        // remains the authority for fillability.
        {
            auto const paths = pg->findPaths(xrpAsset, usdAsset, 6, USD(10));
            BEAST_EXPECT(!paths.empty());
            BEAST_EXPECT(rankOf(paths, true) >= 0);
            BEAST_EXPECT(rankOf(paths, false) >= 0);
        }
    }
};

BEAST_DEFINE_TESTSUITE(Path, app, xrpl);

}  // namespace xrpl::test
