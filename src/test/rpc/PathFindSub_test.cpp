//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2026 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without  fee  is hereby granted, provided that the above
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

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl {
namespace test {

/**
 * Unit / integration coverage for concurrent path_find subscription machinery:
 * incremental revalidate, multi-session cache sharing, six-path shape, mid-close
 * revalidate-only waves, cache counters, and partial-liquidity behavior after
 * covering-path removal.
 *
 * Complements stock xrpl.app.Path / PathMPT (one-shot ripple_path_find) and
 * xrpl.rpc.AssetCache (direct cache unit tests including TSan-friendly
 * concurrency).
 *
 * updateAll is always scheduled on the JobQueue (JtClient) so
 * PathRequestManager's fork-join steady revalidate (JtPathFindWork) has free
 * worker threads. Calling updateAll on the test thread deadlocks when more than
 * one established session is revalidated in parallel.
 */
class PathFindSub_test : public beast::unit_test::Suite
{
    static json::Value
    pfCreate(
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& dstAmt,
        std::optional<std::string> const& srcCurrency = std::nullopt)
    {
        json::Value req;
        req[jss::subcommand] = "create";
        req[jss::source_account] = src.human();
        req[jss::destination_account] = dst.human();
        req[jss::destination_amount] = dstAmt.getJson(JsonOptions::Values::None);
        if (srcCurrency)
        {
            auto& sc = (req[jss::source_currencies] = json::ValueType::Array);
            json::Value c;
            c[jss::currency] = *srcCurrency;
            sc.append(c);
        }
        return req;
    }

    std::optional<json::Value>
    waitPathFindUpdate(
        WSClient& wsc,
        std::chrono::milliseconds timeout = std::chrono::seconds{3},
        bool requireAlts = false)
    {
        return wsc.findMsg(timeout, [&](json::Value const& jv) {
            if (!jv.isMember(jss::type) || jv[jss::type] != "path_find")
                return false;
            if (!requireAlts)
                return true;
            return jv.isMember(jss::alternatives) && jv[jss::alternatives].isArray() &&
                jv[jss::alternatives].size() > 0;
        });
    }

    void
    drainPathFind(WSClient& wsc)
    {
        using namespace std::chrono_literals;
        while (wsc.findMsg(50ms, [](json::Value const& jv) {
            return jv.isMember(jss::type) && jv[jss::type] == "path_find";
        }))
        {
        }
    }

    /**
     * Run updateAll on a JobQueue worker so JtPathFindWork fan-out has free
     * threads. Returns false if the job did not finish in time.
     */
    bool
    runUpdateAll(
        jtx::Env& env,
        std::shared_ptr<ReadView const> const& ledger,
        bool midClose = false)
    {
        using namespace std::chrono_literals;
        auto done = std::make_shared<std::atomic<bool>>(false);
        bool const queued = env.app().getJobQueue().addJob(
            JtClient, "PathFindSub-updateAll", [done, &env, ledger, midClose]() {
                env.app().getPathRequestManager().updateAll(ledger, midClose);
                done->store(true, std::memory_order_release);
            });
        if (!queued)
        {
            // Fallback: only safe with ≤1 established session (no fan-out).
            env.app().getPathRequestManager().updateAll(ledger, midClose);
            return true;
        }
        for (int i = 0; i < 200; ++i)
        {
            if (done->load(std::memory_order_acquire))
                return true;
            std::this_thread::sleep_for(25ms);
        }
        return done->load(std::memory_order_acquire);
    }

    void
    waveClosed(jtx::Env& env)
    {
        env.close();
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
    }

    jtx::Env
    makeEnv(bool multiWorker = true)
    {
        using namespace jtx;
        return Env(*this, envconfig([multiWorker](std::unique_ptr<Config> cfg) {
            // Multi-worker envs exercise parallel steady revalidate. Default
            // stand-alone is 1 JobQueue worker; runParallel falls back to
            // serial there (see testSingleWorkerMultiSessionNoHang).
            if (multiWorker)
            {
                cfg->forceMultiThread = true;
                cfg->workers = 4;
            }
            else
            {
                cfg->forceMultiThread = false;
                cfg->workers = 1;
            }
            cfg->pathFullSearchInterval = 2;
            cfg->pathCacheReuseLedgers = 4;
            cfg->pathMidCloseDelay = std::chrono::milliseconds{200};
            cfg->pathFindLineChunkSize = 64;
            return cfg;
        }));
    }

    void
    setupUsdCorridor(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& alice,
        jtx::Account const& bob)
    {
        using namespace jtx;
        auto const usd = gw["USD"];
        env.fund(XRP(100000), alice, bob, gw);
        env.close();
        env.trust(usd(10000), alice);
        env.trust(usd(10000), bob);
        env(pay(gw, alice, usd(5000)));
        env(pay(gw, bob, usd(100)));
        env.close();
    }

    void
    testRevalidateAcrossCloses()
    {
        testcase("revalidate: second closed-ledger update keeps alternatives");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv();
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        setupUsdCorridor(env, gw, alice, bob);

        auto wsc = makeWSClient(env.app().config());
        auto const create = wsc->invoke("path_find", pfCreate(alice, bob, bob["USD"](20), "USD"));
        auto const& cr = create[jss::result];
        BEAST_EXPECT(!cr.isMember(jss::error));
        BEAST_EXPECT(cr.isMember(jss::alternatives));
        BEAST_EXPECT(cr[jss::alternatives].size() >= 1);

        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        auto first = waitPathFindUpdate(*wsc, 5s, /*requireAlts=*/true);
        BEAST_EXPECT(first);
        drainPathFind(*wsc);

        auto const hitsBefore = [&]() -> double {
            auto gc = env.rpc("get_counts")[jss::result];
            if (gc.isMember("pathfind_cache_hits"))
                return gc["pathfind_cache_hits"].asDouble();
            return 0;
        }();

        waveClosed(env);
        auto second = waitPathFindUpdate(*wsc, 5s, /*requireAlts=*/true);
        BEAST_EXPECT(second);
        if (second)
        {
            BEAST_EXPECT((*second)[jss::alternatives].isArray());
            BEAST_EXPECT((*second)[jss::alternatives].size() >= 1);
            if (second->isMember(jss::full_reply))
                BEAST_EXPECT((*second)[jss::full_reply].asBool());
        }
        drainPathFind(*wsc);

        auto gc = env.rpc("get_counts")[jss::result];
        BEAST_EXPECT(gc.isMember("pathfind_cache_hits"));
        BEAST_EXPECT(gc.isMember("pathfind_cache_misses"));
        BEAST_EXPECT(gc.isMember("pathfind_cache_lines"));
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() > 0);
        BEAST_EXPECT(gc["pathfind_cache_hits"].asDouble() >= hitsBefore);

        waveClosed(env);
        auto third = waitPathFindUpdate(*wsc, 5s, /*requireAlts=*/true);
        BEAST_EXPECT(third);

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        auto closed = wsc->invoke("path_find", closeReq)[jss::result];
        BEAST_EXPECT(!closed.isMember(jss::error) || closed[jss::status] == "success");
        wsc.reset();

        for (int i = 0; i < 40; ++i)
        {
            gc = env.rpc("get_counts")[jss::result];
            if (gc["pathfind_cache_lines"].asDouble() == 0)
                break;
            std::this_thread::sleep_for(25ms);
        }
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() == 0);
    }

    void
    testMultiSessionSharedCache()
    {
        testcase("multi-session: shared cache live while sessions open, reclaims on close");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv();
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dan{"dan"};
        setupUsdCorridor(env, gw, alice, bob);
        env.fund(XRP(100000), carol, dan);
        env.close();
        env.trust(gw["USD"](10000), carol);
        env.trust(gw["USD"](10000), dan);
        env(pay(gw, carol, gw["USD"](2000)));
        env(pay(gw, dan, gw["USD"](50)));
        env.close();

        constexpr int kSessions = 4;
        std::vector<std::unique_ptr<WSClient>> clients;
        clients.reserve(kSessions);
        std::vector<std::pair<Account, Account>> pairs = {
            {alice, bob}, {carol, dan}, {alice, dan}, {carol, bob}};

        for (int i = 0; i < kSessions; ++i)
        {
            clients.push_back(makeWSClient(env.app().config()));
            auto const& [src, dst] = pairs[static_cast<std::size_t>(i)];
            auto jr = clients.back()->invoke(
                "path_find", pfCreate(src, dst, dst["USD"](5), "USD"))[jss::result];
            BEAST_EXPECT(!jr.isMember(jss::error));
            BEAST_EXPECT(jr.isMember(jss::alternatives));
        }

        // First-update wave for all new sessions (JobQueue so fan-out is safe).
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        int firstWave = 0;
        for (auto& c : clients)
        {
            if (waitPathFindUpdate(*c, 5s, true))
                ++firstWave;
            drainPathFind(*c);
        }
        BEAST_EXPECT(firstWave >= kSessions);

        auto gc = env.rpc("get_counts")[jss::result];
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() > 0);
        auto const linesWhileOpen = gc["pathfind_cache_lines"].asDouble();

        // Steady closed wave across all sessions (exercises parallel revalidate).
        waveClosed(env);
        int refreshed = 0;
        for (auto& c : clients)
        {
            if (waitPathFindUpdate(*c, 5s, /*requireAlts=*/false))
                ++refreshed;
            drainPathFind(*c);
        }
        BEAST_EXPECT(refreshed == kSessions);

        gc = env.rpc("get_counts")[jss::result];
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() > 0);
        // Shared hubs should not thrash to empty between sessions.
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() >= linesWhileOpen * 0.5);

        // Close half — cache should remain while others hold pins.
        for (int i = 0; i < kSessions / 2; ++i)
        {
            json::Value closeReq;
            closeReq[jss::subcommand] = "close";
            (void)clients[static_cast<std::size_t>(i)]->invoke("path_find", closeReq);
            clients[static_cast<std::size_t>(i)].reset();
        }
        gc = env.rpc("get_counts")[jss::result];
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() > 0);

        for (auto& c : clients)
        {
            if (!c)
                continue;
            json::Value closeReq;
            closeReq[jss::subcommand] = "close";
            (void)c->invoke("path_find", closeReq);
        }
        clients.clear();

        for (int i = 0; i < 40; ++i)
        {
            gc = env.rpc("get_counts")[jss::result];
            if (gc["pathfind_cache_lines"].asDouble() == 0)
                break;
            std::this_thread::sleep_for(25ms);
        }
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() == 0);
    }

    void
    testSingleWorkerMultiSessionNoHang()
    {
        // Regression: runParallel used to fork-join JtPathFindWork from inside
        // a JobQueue thread. With workers=1 (stand-alone default) the wait
        // never completed and wedged pathfinding. Must finish promptly.
        testcase("single worker: multi-session steady wave does not hang");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv(/*multiWorker=*/false);
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dan{"dan"};
        setupUsdCorridor(env, gw, alice, bob);
        env.fund(XRP(100000), carol, dan);
        env.close();
        env.trust(gw["USD"](10000), carol);
        env.trust(gw["USD"](10000), dan);
        env(pay(gw, carol, gw["USD"](2000)));
        env(pay(gw, dan, gw["USD"](50)));
        env.close();

        constexpr int kSessions = 3;
        std::vector<std::unique_ptr<WSClient>> clients;
        std::vector<std::pair<Account, Account>> pairs = {{alice, bob}, {carol, dan}, {alice, dan}};

        for (int i = 0; i < kSessions; ++i)
        {
            clients.push_back(makeWSClient(env.app().config()));
            auto const& [src, dst] = pairs[static_cast<std::size_t>(i)];
            auto jr = clients.back()->invoke(
                "path_find", pfCreate(src, dst, dst["USD"](5), "USD"))[jss::result];
            BEAST_EXPECT(!jr.isMember(jss::error));
        }

        // First wave (new sessions) then steady wave — both used to hang when
        // steadyUpdates.size() > 1 on a 1-worker queue.
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        for (auto& c : clients)
            drainPathFind(*c);

        waveClosed(env);
        int refreshed = 0;
        for (auto& c : clients)
        {
            if (waitPathFindUpdate(*c, 5s, /*requireAlts=*/false))
                ++refreshed;
            drainPathFind(*c);
        }
        BEAST_EXPECT(refreshed == kSessions);

        for (auto& c : clients)
        {
            json::Value closeReq;
            closeReq[jss::subcommand] = "close";
            (void)c->invoke("path_find", closeReq);
        }
    }

    void
    testSixPathShape()
    {
        testcase("path set shape: up to six alternatives (no covering spare)");
        using namespace jtx;
        Env env = makeEnv();
        Account const g1{"g1"};
        Account const g2{"g2"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const m1{"m1"};
        Account const m2{"m2"};
        Account const m3{"m3"};
        Account const m4{"m4"};

        env.fund(XRP(1000000), alice, bob, g1, g2, m1, m2, m3, m4);
        env.close();

        env.trust(g1["USD"](100000), alice);
        env.trust(g2["HKD"](100000), bob);
        env(pay(g1, alice, g1["USD"](50000)));
        env.close();

        for (auto const& m : {m1, m2, m3, m4})
        {
            env.trust(g1["USD"](100000), m);
            env.trust(g2["HKD"](100000), m);
            env(pay(g1, m, g1["USD"](10000)));
            env(pay(g2, m, g2["HKD"](10000)));
        }
        env.close();

        env(offer(m1, g1["USD"](1000), g2["HKD"](1000)));
        env(offer(m2, g1["USD"](1000), g2["HKD"](990)));
        env(offer(m3, g1["USD"](1000), g2["HKD"](980)));
        env(offer(m4, g1["USD"](1000), g2["HKD"](970)));
        env(offer(m1, g1["USD"](500), XRP(500)));
        env(offer(m1, XRP(500), g2["HKD"](500)));
        env(offer(m2, g1["USD"](500), XRP(480)));
        env(offer(m2, XRP(480), g2["HKD"](500)));
        env.close();

        json::Value params;
        params[jss::source_account] = alice.human();
        params[jss::destination_account] = bob.human();
        params[jss::destination_amount] = bob["HKD"](10).value().getJson(JsonOptions::Values::None);
        {
            auto& sc = (params[jss::source_currencies] = json::ValueType::Array);
            json::Value c;
            c[jss::currency] = "USD";
            c[jss::issuer] = g1.human();
            sc.append(c);
        }

        auto const resp = env.rpc("json", "ripple_path_find", to_string(params));
        auto const& result = resp[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result.isMember(jss::alternatives));
        auto const& alts = result[jss::alternatives];
        BEAST_EXPECT(alts.isArray());
        BEAST_EXPECT(alts.size() >= 1);

        unsigned maxPathsInAlt = 0;
        for (unsigned i = 0; i < alts.size(); ++i)
        {
            auto const& alt = alts[i];
            if (!alt.isMember(jss::paths_computed))
                continue;
            auto const n = alt[jss::paths_computed].size();
            if (n > maxPathsInAlt)
                maxPathsInAlt = n;
            BEAST_EXPECT(n <= static_cast<unsigned>(rpc::tuning::kPathFindMaxPaths));
        }
        BEAST_EXPECT(maxPathsInAlt >= 1);
        BEAST_EXPECT(rpc::tuning::kPathFindMaxPaths == 6);
    }

    void
    testPartialLiquidityNoCoveringSpare()
    {
        testcase("partial liquidity: no covering-path retry (best-effort alts)");
        using namespace jtx;
        Env env = makeEnv();
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const thin{"thin"};
        Account const eve{"eve"};

        env.fund(XRP(100000), alice, bob, gw, thin, eve);
        env.close();
        env.trust(gw["USD"](10000), alice);
        env.trust(gw["USD"](10000), bob);
        env.trust(gw["USD"](10000), thin);
        env(pay(gw, alice, gw["USD"](5000)));
        env(pay(gw, thin, gw["USD"](5)));
        env.close();
        env(offer(thin, XRP(5), gw["USD"](5)));
        env.close();

        // eve pays bob USD using only XRP via a 5-USD book for dest 100 USD.
        // Covering-path removal: failed maxPaths set is dropped (empty alts OK).
        json::Value params;
        params[jss::source_account] = eve.human();
        params[jss::destination_account] = bob.human();
        params[jss::destination_amount] =
            bob["USD"](100).value().getJson(JsonOptions::Values::None);
        {
            auto& sc = (params[jss::source_currencies] = json::ValueType::Array);
            json::Value c;
            c[jss::currency] = "XRP";
            sc.append(c);
        }

        auto const resp = env.rpc("json", "ripple_path_find", to_string(params));
        auto const& result = resp[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result.isMember(jss::alternatives));
        BEAST_EXPECT(result[jss::alternatives].isArray());
        BEAST_EXPECT(result[jss::alternatives].size() <= 1);
        if (result[jss::alternatives].size() == 1 &&
            result[jss::alternatives][0u].isMember(jss::paths_computed))
        {
            BEAST_EXPECT(
                result[jss::alternatives][0u][jss::paths_computed].size() <=
                static_cast<unsigned>(rpc::tuning::kPathFindMaxPaths));
        }
    }

    void
    testStaggeredRediscoverySurvivesManyCloses()
    {
        testcase("stagger: many closes keep alternatives (interval=2)");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv();
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        setupUsdCorridor(env, gw, alice, bob);

        auto wsc = makeWSClient(env.app().config());
        auto jr =
            wsc->invoke("path_find", pfCreate(alice, bob, bob["USD"](10), "USD"))[jss::result];
        BEAST_EXPECT(!jr.isMember(jss::error));

        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        BEAST_EXPECT(waitPathFindUpdate(*wsc, 5s, true));
        drainPathFind(*wsc);

        int updates = 0;
        for (int i = 0; i < 6; ++i)
        {
            waveClosed(env);
            if (waitPathFindUpdate(*wsc, 5s, true))
                ++updates;
            drainPathFind(*wsc);
        }
        BEAST_EXPECT(updates >= 4);

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wsc->invoke("path_find", closeReq);
    }

    void
    testMidCloseRevalidateOnly()
    {
        testcase("mid-close: revalidate-only wave pushes updates");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv();
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        setupUsdCorridor(env, gw, alice, bob);

        auto wsc = makeWSClient(env.app().config());
        auto jr =
            wsc->invoke("path_find", pfCreate(alice, bob, bob["USD"](10), "USD"))[jss::result];
        BEAST_EXPECT(!jr.isMember(jss::error));

        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        BEAST_EXPECT(waitPathFindUpdate(*wsc, 5s, true));
        drainPathFind(*wsc);

        // Mid-close on open ledger: revalidateOnly, does not pin lastIndex_.
        BEAST_EXPECT(runUpdateAll(env, env.current(), /*midClose=*/true));
        auto mid = waitPathFindUpdate(*wsc, 5s, /*requireAlts=*/false);
        BEAST_EXPECT(mid);
        drainPathFind(*wsc);

        // Same-seq closed wave still runs (mid-close did not pin lastIndex_).
        waveClosed(env);
        auto after = waitPathFindUpdate(*wsc, 5s, true);
        BEAST_EXPECT(after);

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wsc->invoke("path_find", closeReq);
    }

public:
    void
    run() override
    {
        testRevalidateAcrossCloses();
        testMultiSessionSharedCache();
        testSingleWorkerMultiSessionNoHang();
        testSixPathShape();
        testPartialLiquidityNoCoveringSpare();
        testStaggeredRediscoverySurvivesManyCloses();
        testMidCloseRevalidateOnly();
    }
};

BEAST_DEFINE_TESTSUITE(PathFindSub, rpc, xrpl);

}  // namespace test
}  // namespace xrpl
