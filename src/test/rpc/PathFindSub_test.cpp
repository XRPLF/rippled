#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

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
 * updateAll is scheduled on the JobQueue (JtClient) to mirror production
 * (JtUpdatePf / JtRpc). Steady revalidate only fork-joins when JobQueue
 * workers >= 3; with fewer workers it runs serially. Multi-session cases still
 * go through the JobQueue so the workers < 3 serial path and the workers >= 3
 * fan-out path are both exercised under realistic scheduling.
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

    static std::optional<json::Value>
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

    static void
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
     * Run updateAll on a JobQueue worker (same pool that production uses for
     * JtUpdatePf / JtRpc). With workers >= 3, steady revalidate may fan out
     * JtPathFindWork from that worker; with workers < 3 it stays serial.
     * Returns false if the job did not finish in time.
     */
    static bool
    runUpdateAll(
        jtx::Env& env,
        std::shared_ptr<ReadView const> const& ledger,
        bool midClose = false)
    {
        using namespace std::chrono_literals;
        // cv notify — avoid 200×25ms wall sleep that can exceed unit-test budget
        // under load while still allowing a short absolute deadline.
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto mtx = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();
        bool const queued = env.app().getJobQueue().addJob(
            JtClient, "PathFindSub-updateAll", [done, mtx, cv, &env, ledger, midClose]() {
                env.app().getPathRequestManager().updateAll(ledger, midClose);
                {
                    std::scoped_lock const lk(*mtx);
                    done->store(true, std::memory_order_release);
                }
                cv->notify_one();
            });
        if (!queued)
        {
            // Queue full / stopping: run inline. Safe for multi-session when
            // workers < 3 (serial path). Prefer the JobQueue path above for
            // workers >= 3 so fan-out is exercised from a real pool thread.
            env.app().getPathRequestManager().updateAll(ledger, midClose);
            return true;
        }
        std::unique_lock lk(*mtx);
        return cv->wait_for(lk, 5s, [&] { return done->load(std::memory_order_acquire); });
    }

    void
    waveClosed(jtx::Env& env)
    {
        env.close();
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
    }

    jtx::Env
    makeEnv(bool multiWorker = true, int workers = 4)
    {
        using namespace jtx;
        return Env(*this, envconfig([multiWorker, workers](std::unique_ptr<Config> cfg) {
            // Stand-alone without forceMultiThread → Application always builds
            // 1 JobQueue thread even if [workers] is higher (see Application.cpp).
            // runParallel is serial for workers < 3 and may fan out for workers >= 3
            // (batch ≤ workers - 1). Default multiWorker workers=4 exercises fan-out.
            if (multiWorker)
            {
                cfg->forceMultiThread = true;
                cfg->workers = workers;
            }
            else
            {
                cfg->forceMultiThread = false;
                // Intentionally cfg.workers=2 while JobQueue is still 1-thread —
                // catches jobQueueWorkerCount checking workers before standalone.
                cfg->workers = 2;
            }
            cfg->pathFullSearchInterval = 2;
            cfg->pathCacheReuseLedgers = 4;
            cfg->pathMidCloseDelay = std::chrono::milliseconds{200};
            cfg->pathFindLineChunkSize = 64;
            return cfg;
        }));
    }

    static void
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

        // First-update wave for all new sessions (always serial; JobQueue path).
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
    multiSessionSteadyNoHang(jtx::Env& env, int expectedSessions)
    {
        using namespace jtx;
        using namespace std::chrono_literals;
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

        std::vector<std::unique_ptr<WSClient>> clients;
        std::vector<std::pair<Account, Account>> pairs = {{alice, bob}, {carol, dan}, {alice, dan}};
        BEAST_EXPECT(static_cast<int>(pairs.size()) >= expectedSessions);

        for (int i = 0; i < expectedSessions; ++i)
        {
            clients.push_back(makeWSClient(env.app().config()));
            auto const& [src, dst] = pairs[static_cast<std::size_t>(i)];
            auto jr = clients.back()->invoke(
                "path_find", pfCreate(src, dst, dst["USD"](5), "USD"))[jss::result];
            BEAST_EXPECT(!jr.isMember(jss::error));
        }

        // First wave (new sessions) then steady wave.
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
        BEAST_EXPECT(refreshed == expectedSessions);

        for (auto& c : clients)
        {
            json::Value closeReq;
            closeReq[jss::subcommand] = "close";
            (void)c->invoke("path_find", closeReq);
        }
    }

    void
    testSingleWorkerMultiSessionNoHang()
    {
        // Regression: stand-alone without forceMultiThread has 1 JobQueue
        // thread even when cfg.workers > 1. jobQueueWorkerCount must report 1
        // (standalone before workers) so runParallel stays serial (workers < 3).
        // Fan-out on a 1-thread pool hangs forever on doneCv.
        testcase("single worker: multi-session steady wave does not hang");
        auto env = makeEnv(/*multiWorker=*/false);
        multiSessionSteadyNoHang(env, /*expectedSessions=*/3);
    }

    void
    testTwoWorkerMultiSessionNoHang()
    {
        // Regression: forceMultiThread + workers=2 is a real 2-thread JobQueue.
        // runParallel must stay serial (workers < 3). Fan-out here hangs when a
        // second updateAll blocks on waveMutex_ — zero threads left for
        // JtPathFindWork while the parent waits on doneCv.
        testcase("two workers: multi-session steady wave does not hang");
        using namespace std::chrono_literals;
        auto env = makeEnv(/*multiWorker=*/true, /*workers=*/2);

        // Establish sessions and run a closed wave (serial: workers == 2 < 3).
        multiSessionSteadyNoHang(env, /*expectedSessions=*/3);

        // Re-open sessions and race closed + mid-close updateAll on the two
        // workers (the historical hang shape).
        using namespace jtx;
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dan{"dan"};

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
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        for (auto& c : clients)
            drainPathFind(*c);

        auto doneClosed = std::make_shared<std::atomic<bool>>(false);
        auto doneMid = std::make_shared<std::atomic<bool>>(false);
        auto const closed = env.closed();
        auto const open = env.current();
        bool const q1 = env.app().getJobQueue().addJob(
            JtClient, "PathFindSub-closed", [doneClosed, &env, closed]() {
                env.app().getPathRequestManager().updateAll(closed, /*midClose=*/false);
                doneClosed->store(true, std::memory_order_release);
            });
        bool const q2 =
            env.app().getJobQueue().addJob(JtClient, "PathFindSub-mid", [doneMid, &env, open]() {
                env.app().getPathRequestManager().updateAll(open, /*midClose=*/true);
                doneMid->store(true, std::memory_order_release);
            });
        BEAST_EXPECT(q1 && q2);

        bool bothDone = false;
        for (int i = 0; i < 400; ++i)
        {
            if (doneClosed->load(std::memory_order_acquire) &&
                doneMid->load(std::memory_order_acquire))
            {
                bothDone = true;
                break;
            }
            std::this_thread::sleep_for(25ms);
        }
        BEAST_EXPECT(bothDone);

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
        for (auto const& alt : alts)
        {
            if (!alt.isMember(jss::paths_computed))
                continue;
            auto const n = alt[jss::paths_computed].size();
            maxPathsInAlt = std::max(n, maxPathsInAlt);
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

    /**
     * Short books with token liquidity must not hide a longer corridor that
     * can actually deliver the destination. rankPaths used to stop after one
     * short path passed smallestUsefulAmount.
     */
    void
    testRankKeepsLookingForCoveringLiquidity()
    {
        testcase("rankPaths: keep pricing until top paths can cover dest");
        using namespace jtx;
        Env env = makeEnv();
        Account const gUsd{"gUsd"};
        Account const gHkd{"gHkd"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const thin{"thin"};
        Account const fat{"fat"};

        env.fund(XRP(1000000), alice, bob, gUsd, gHkd, thin, fat);
        env.close();

        env.trust(gUsd["USD"](100000), alice);
        env.trust(gHkd["HKD"](100000), bob);
        env.trust(gUsd["USD"](100000), thin);
        env.trust(gHkd["HKD"](100000), thin);
        env.trust(gUsd["USD"](100000), fat);
        env.trust(gHkd["HKD"](100000), fat);
        env(pay(gUsd, alice, gUsd["USD"](50000)));
        env(pay(gUsd, thin, gUsd["USD"](10)));
        env(pay(gHkd, thin, gHkd["HKD"](10)));
        env(pay(gUsd, fat, gUsd["USD"](20000)));
        env(pay(gHkd, fat, gHkd["HKD"](20000)));
        env.close();

        // Short: 5 HKD of USD/HKD book. Long: fat holds a 2000 book.
        env(offer(thin, gUsd["USD"](5), gHkd["HKD"](5)));
        env(offer(fat, gUsd["USD"](2000), gHkd["HKD"](2000)));
        env.close();

        json::Value params;
        params[jss::source_account] = alice.human();
        params[jss::destination_account] = bob.human();
        params[jss::destination_amount] =
            bob["HKD"](500).value().getJson(JsonOptions::Values::None);
        {
            auto& sc = (params[jss::source_currencies] = json::ValueType::Array);
            json::Value c;
            c[jss::currency] = "USD";
            c[jss::issuer] = gUsd.human();
            sc.append(c);
        }

        auto const resp = env.rpc("json", "ripple_path_find", to_string(params));
        auto const& result = resp[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result.isMember(jss::alternatives));
        BEAST_EXPECT(result[jss::alternatives].isArray());
        BEAST_EXPECT(result[jss::alternatives].size() >= 1);
        if (result[jss::alternatives].size() >= 1)
        {
            BEAST_EXPECT(result[jss::alternatives][0u].isMember(jss::source_amount));
            BEAST_EXPECT(result[jss::alternatives][0u].isMember(jss::paths_computed));
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
        if (mid)
        {
            // Open views inherit the parent hash and only bump seq. The
            // reply must report the closed ledger identity, not (parent
            // hash, open seq).
            auto const closed = env.closed();
            auto const open = env.current();
            BEAST_EXPECT(mid->isMember(jss::ledger_hash));
            BEAST_EXPECT(mid->isMember(jss::ledger_index));
            if (mid->isMember(jss::ledger_hash) && mid->isMember(jss::ledger_index))
            {
                auto const hash = (*mid)[jss::ledger_hash].asString();
                auto const idx = (*mid)[jss::ledger_index].asUInt();
                BEAST_EXPECT(hash == to_string(closed->header().hash));
                BEAST_EXPECT(idx == closed->seq());
                BEAST_EXPECT(idx != open->seq() || hash != to_string(open->header().hash));
            }
        }
        drainPathFind(*wsc);

        // Same-seq closed wave still runs (mid-close did not pin lastIndex_).
        waveClosed(env);
        auto after = waitPathFindUpdate(*wsc, 5s, true);
        BEAST_EXPECT(after);

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wsc->invoke("path_find", closeReq);
    }

    /**
     * Mid-close must not consume pathFindNewRequest_. If it did, LedgerMaster::
     * updatePaths can exit with "Nothing to do" and a brand-new path_find client
     * waits until the next closed ledger for its first full Pathfinder result.
     */
    void
    testMidClosePreservesNewSubscriptionSignal()
    {
        testcase("mid-close: does not swallow new path_find subscription signal");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv();
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        setupUsdCorridor(env, gw, alice, bob);
        env.fund(XRP(100000), carol);
        env.close();
        env.trust(gw["USD"](10000), carol);
        env(pay(gw, carol, gw["USD"](5000)));
        env.close();

        // Established session A so mid-close has work and the timer path is live.
        auto wscA = makeWSClient(env.app().config());
        auto jrA =
            wscA->invoke("path_find", pfCreate(alice, bob, bob["USD"](10), "USD"))[jss::result];
        BEAST_EXPECT(!jrA.isMember(jss::error));
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        BEAST_EXPECT(waitPathFindUpdate(*wscA, 5s, true));
        drainPathFind(*wscA);

        auto& lm = env.app().getLedgerMaster();
        // Drop any residual create signal from session A.
        (void)lm.isNewPathRequest();

        // Brand-new client B. makePathRequest sets pathFindNewRequest_ and may
        // queue PthFindNewReq. Mid-close must not clear that flag.
        auto wscB = makeWSClient(env.app().config());
        auto jrB =
            wscB->invoke("path_find", pfCreate(carol, bob, bob["USD"](5), "USD"))[jss::result];
        BEAST_EXPECT(!jrB.isMember(jss::error));

        // Pure mid-close wave (same entry as periodic revalidate). Must not
        // first-Pathfind B (isFirst skip) and must not steal the create signal.
        BEAST_EXPECT(runUpdateAll(env, env.current(), /*midClose=*/true));

        // Flag preservation: re-arm a create signal and ensure mid-close leaves
        // it set. Concurrent updatePaths may drain the flag after mid-close
        // releases waveMutex_, so retry a few times; with the bug mid-close
        // always consumes and preserved stays 0 when mid-close runs with the
        // flag set.
        int attempted = 0;
        int preserved = 0;
        for (int i = 0; i < 40; ++i)
        {
            (void)lm.isNewPathRequest();
            if (!lm.newPathRequest())
                continue;
            ++attempted;
            // Inline mid-close holds waveMutex_ so a concurrent create updateAll
            // blocks before isNewPathRequest — only mid-close could clear it.
            env.app().getPathRequestManager().updateAll(env.current(), /*midClose=*/true);
            if (lm.isNewPathRequest())
                ++preserved;
        }
        BEAST_EXPECT(attempted > 0);
        // Fix: mid-close never consumes → preserved tracks attempted (minus a
        // rare post-unlock drain). Bug: mid-close always consumes → preserved≈0.
        BEAST_EXPECT(preserved * 2 >= attempted);

        // B must still receive a first full result without waiting for a new
        // closed ledger (create-style open wave / queued PthFindNewReq).
        if (!waitPathFindUpdate(*wscB, 100ms, true))
        {
            // Explicit create wake if the JobQueue job already lost the race.
            BEAST_EXPECT(runUpdateAll(env, env.current(), /*midClose=*/false));
        }
        BEAST_EXPECT(waitPathFindUpdate(*wscB, 5s, true));

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wscA->invoke("path_find", closeReq);
        (void)wscB->invoke("path_find", closeReq);
    }

    /**
     * Soft auto-source cap is 16. Pure alphabetical order puts "XRP" after
     * many 3-letter IOU codes, so multi-currency accounts would drop XRP and
     * miss the usual cheapest route. XRP must be retained under the soft cap.
     */
    void
    testAutoSourceKeepsXrpUnderSoftCap()
    {
        testcase("auto source: XRP retained under soft cap with many IOUs");
        using namespace jtx;
        using namespace std::chrono_literals;
        Env env = makeEnv();
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const gw{"gateway"};
        // More than kMaxAutoSrcCurSub (16) IOU currencies that sort before "XRP".
        constexpr int kIouCount = 20;
        env.fund(XRP(1000000), alice, bob, gw);
        env.close();

        for (int i = 0; i < kIouCount; ++i)
        {
            // "A00".."A19" — all lexicographically before "XRP".
            char code[4] = {
                'A', static_cast<char>('0' + (i / 10)), static_cast<char>('0' + (i % 10)), '\0'};
            auto const iou = gw[code];
            env.trust(iou(10000), alice);
            env(pay(gw, alice, iou(100)));
        }
        // Only viable payment path: spend XRP into bob's gw/USD via book.
        // None of the Axx IOUs have books, so dropping XRP under the soft cap
        // would yield no alternatives.
        env.trust(gw["USD"](10000), bob);
        env(pay(gw, bob, gw["USD"](1)));
        env(offer(gw, XRP(100), gw["USD"](100)));
        env.close();

        auto wsc = makeWSClient(env.app().config());
        // No source_currencies → auto set (soft-capped at 16).
        auto jr = wsc->invoke("path_find", pfCreate(alice, bob, gw["USD"](10)))[jss::result];
        BEAST_EXPECT(!jr.isMember(jss::error));
        // Create reply: warning plus warnings[] so a later envelope
        // `warning: load` cannot hide the truncation notice.
        BEAST_EXPECT(jr.isMember(jss::warning));
        if (jr.isMember(jss::warning))
            BEAST_EXPECT(jr[jss::warning].asString() == "path_source_currencies_truncated");
        BEAST_EXPECT(jr.isMember(jss::warnings) && jr[jss::warnings].isArray());
        if (jr.isMember(jss::warnings) && jr[jss::warnings].isArray())
        {
            bool sawTrunc = false;
            for (auto const& w : jr[jss::warnings])
            {
                BEAST_EXPECT(w.isObject() && w.isMember(jss::id) && w.isMember(jss::message));
                if (w.isObject() && w.isMember(jss::id) &&
                    w[jss::id].asInt() == WarnRpcPathSourceCurrenciesTruncated)
                    sawTrunc = true;
            }
            BEAST_EXPECT(sawTrunc);
        }
        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        auto upd = waitPathFindUpdate(*wsc, 5s, true);
        BEAST_EXPECT(upd);
        if (upd)
        {
            BEAST_EXPECT(upd->isMember(jss::warning));
            if (upd->isMember(jss::warning))
                BEAST_EXPECT((*upd)[jss::warning].asString() == "path_source_currencies_truncated");
            BEAST_EXPECT(upd->isMember(jss::warnings) && (*upd)[jss::warnings].isArray());
            if (upd->isMember(jss::warnings) && (*upd)[jss::warnings].isArray() &&
                (*upd)[jss::warnings].size() > 0)
            {
                auto const& w = (*upd)[jss::warnings][0u];
                BEAST_EXPECT(w.isObject() && w.isMember(jss::id) && w.isMember(jss::message));
                if (w.isObject() && w.isMember(jss::id))
                    BEAST_EXPECT(w[jss::id].asInt() == WarnRpcPathSourceCurrenciesTruncated);
            }
        }

        bool sawXrpSource = false;
        if (upd && upd->isMember(jss::alternatives) && (*upd)[jss::alternatives].isArray())
        {
            for (auto const& alt : (*upd)[jss::alternatives])
            {
                if (!alt.isMember(jss::source_amount))
                    continue;
                auto const& sa = alt[jss::source_amount];
                // Native XRP is often a drops string; IOUs are objects with currency.
                if (sa.isString() ||
                    (sa.isObject() &&
                     (!sa.isMember(jss::currency) || sa[jss::currency].asString() == "XRP")))
                {
                    sawXrpSource = true;
                    break;
                }
            }
        }
        BEAST_EXPECT(sawXrpSource);

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wsc->invoke("path_find", closeReq);
    }

    /**
     * The shared AssetCache is a strong member for the life of any session.
     * Creates pass authoritative=false; they must still rebuild when the
     * requested closed ledger is more than 8 sequences ahead (historical
     * getLineCache). Without that, doCreate reports ledger_index / routes
     * from an arbitrarily stale view while updatePaths is starved.
     */
    void
    testCreateRebuildsStaleSharedCache()
    {
        testcase("create: rebuilds shared cache more than 8 ledgers behind");
        using namespace jtx;
        using namespace std::chrono_literals;

        // Disable mid-close ticks so they cannot advance the cache during
        // the gap. The regression is specifically non-authoritative create.
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            cfg->workers = 4;
            cfg->pathMidCloseDelay = std::chrono::hours{1};
            cfg->pathCacheReuseLedgers = 4;
            return cfg;
        }));

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        setupUsdCorridor(env, gw, alice, bob);

        auto& prm = env.app().getPathRequestManager();
        auto const firstClosed = env.closed();
        auto cache = prm.getAssetCache(firstClosed, /*authoritative=*/true);
        auto const staleSeq = firstClosed->seq();
        BEAST_EXPECT(cache->getLedger()->seq() == staleSeq);

        // Within the 8-ledger window: non-authoritative create reuses.
        for (int i = 0; i < 3; ++i)
            env.close();
        auto const midClosed = env.closed();
        BEAST_EXPECT(midClosed->seq() > staleSeq);
        BEAST_EXPECT(midClosed->seq() <= staleSeq + 8);
        auto reused = prm.getAssetCache(midClosed, /*authoritative=*/false);
        BEAST_EXPECT(reused->getLedger()->seq() == staleSeq);

        // Jump more than 8 ahead of the cached view. Create (authoritative=
        // false) must rebuild — otherwise a new path_find reports stale
        // ledger_hash / ledger_index and prices from the old balances.
        for (int i = 0; i < 8; ++i)
            env.close();
        auto const nowClosed = env.closed();
        BEAST_EXPECT(nowClosed->seq() > staleSeq + 8);
        auto rebuilt = prm.getAssetCache(nowClosed, /*authoritative=*/false);
        BEAST_EXPECT(rebuilt->getLedger()->seq() == nowClosed->seq());

        // User-visible: a live session pins the (now current) cache, then
        // another large gap, then a new path_find create.
        auto wscA = makeWSClient(env.app().config());
        auto jrA =
            wscA->invoke("path_find", pfCreate(alice, bob, bob["USD"](10), "USD"))[jss::result];
        BEAST_EXPECT(!jrA.isMember(jss::error));
        BEAST_EXPECT(jrA.isMember(jss::ledger_index));
        auto const pinnedSeq = jrA[jss::ledger_index].asUInt();
        BEAST_EXPECT(pinnedSeq == nowClosed->seq());

        for (int i = 0; i < 10; ++i)
            env.close();
        auto const laterClosed = env.closed();
        BEAST_EXPECT(laterClosed->seq() > pinnedSeq + 8);

        auto wscB = makeWSClient(env.app().config());
        auto jrB =
            wscB->invoke("path_find", pfCreate(alice, bob, bob["USD"](5), "USD"))[jss::result];
        BEAST_EXPECT(!jrB.isMember(jss::error));
        BEAST_EXPECT(jrB.isMember(jss::ledger_index));
        BEAST_EXPECT(jrB[jss::ledger_index].asUInt() == laterClosed->seq());
        BEAST_EXPECT(jrB[jss::ledger_index].asUInt() != pinnedSeq);

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wscA->invoke("path_find", closeReq);
        (void)wscB->invoke("path_find", closeReq);
    }

    /**
     * Mid-close is non-authoritative and passes the open ledger. A large
     * forward jump must not force-clear the shared cache onto that OpenView
     * (wipes every session's lines; later updates omit ledger identity).
     * No live subscription here so LedgerMaster::updatePaths does not
     * advance the cache during the gap.
     */
    void
    testMidCloseDoesNotForceClearOpenCache()
    {
        testcase("mid-close: large jump does not rebuild cache onto open ledger");
        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            cfg->workers = 4;
            cfg->pathMidCloseDelay = std::chrono::hours{1};
            return cfg;
        }));

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        setupUsdCorridor(env, gw, alice, bob);

        auto& prm = env.app().getPathRequestManager();
        auto const firstClosed = env.closed();
        auto pinned = prm.getAssetCache(firstClosed, /*authoritative=*/true);
        auto const pinnedSeq = firstClosed->seq();
        BEAST_EXPECT(pinned->getLedger()->seq() == pinnedSeq);
        BEAST_EXPECT(!pinned->getLedger()->open());

        for (int i = 0; i < 10; ++i)
            env.close();
        BEAST_EXPECT(env.current()->open());
        BEAST_EXPECT(env.current()->seq() > pinnedSeq + 8);

        // Same arguments as runPeriodicRevalidate / mid-close updateAll.
        auto mid = prm.getAssetCache(env.current(), /*authoritative=*/false);
        BEAST_EXPECT(!mid->getLedger()->open());
        BEAST_EXPECT(mid->getLedger()->seq() == pinnedSeq);

        // Closed creates still rebuild across the same gap.
        auto rebuilt = prm.getAssetCache(env.closed(), /*authoritative=*/false);
        BEAST_EXPECT(!rebuilt->getLedger()->open());
        BEAST_EXPECT(rebuilt->getLedger()->seq() == env.closed()->seq());
    }

    /**
     * Close one of two live subscriptions while a closed-ledger wave is
     * running. The closing session must not leave leaked pins: after the
     * remaining subscription ends, the shared cache must reclaim to 0.
     */
    void
    testCloseDuringUpdateReclaimsPins()
    {
        testcase("close during updateAll: no leaked pins after last session");
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

        auto wscKeep = makeWSClient(env.app().config());
        auto wscClose = makeWSClient(env.app().config());
        auto jrKeep =
            wscKeep->invoke("path_find", pfCreate(alice, bob, bob["USD"](5), "USD"))[jss::result];
        auto jrClose =
            wscClose->invoke("path_find", pfCreate(carol, dan, dan["USD"](5), "USD"))[jss::result];
        BEAST_EXPECT(!jrKeep.isMember(jss::error));
        BEAST_EXPECT(!jrClose.isMember(jss::error));

        BEAST_EXPECT(runUpdateAll(env, env.closed()));
        drainPathFind(*wscKeep);
        drainPathFind(*wscClose);

        auto gc = env.rpc("get_counts")[jss::result];
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() > 0);

        // Race a closed wave with close of one subscription.
        env.close();
        auto const closed = env.closed();
        auto done = std::make_shared<std::atomic<bool>>(false);
        bool const queued = env.app().getJobQueue().addJob(
            JtClient, "PathFindSub-closeRace", [done, &env, closed]() {
                env.app().getPathRequestManager().updateAll(closed);
                done->store(true, std::memory_order_release);
            });
        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wscClose->invoke("path_find", closeReq);
        wscClose.reset();
        if (queued)
        {
            for (int i = 0; i < 200 && !done->load(std::memory_order_acquire); ++i)
                std::this_thread::sleep_for(25ms);
            BEAST_EXPECT(done->load(std::memory_order_acquire));
        }
        else
        {
            env.app().getPathRequestManager().updateAll(env.closed());
        }

        gc = env.rpc("get_counts")[jss::result];
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() > 0);

        (void)wscKeep->invoke("path_find", closeReq);
        wscKeep.reset();

        for (int i = 0; i < 40; ++i)
        {
            gc = env.rpc("get_counts")[jss::result];
            if (gc["pathfind_cache_lines"].asDouble() == 0)
                break;
            std::this_thread::sleep_for(25ms);
        }
        BEAST_EXPECT(gc["pathfind_cache_lines"].asDouble() == 0);
    }

    /**
     * updatePaths create-wake calls updateAll(open, midClose=false) when a
     * brand-new subscription arrives and no new validated ledger exists.
     * That must not advance the shared cache onto an OpenView, and the
     * streamed update must still carry a matching closed ledger identity.
     */
    void
    testCreateWakeOnOpenReportsClosedIdentity()
    {
        testcase("create-wake: open updateAll reports closed ledger identity");
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
        drainPathFind(*wsc);

        auto& prm = env.app().getPathRequestManager();
        auto const closed = env.closed();
        auto const open = env.current();
        BEAST_EXPECT(open->open());
        BEAST_EXPECT(closed->seq() + 1 == open->seq());

        // Same arguments as LedgerMaster::updatePaths create-wake.
        BEAST_EXPECT(runUpdateAll(env, open, /*midClose=*/false));
        auto upd = waitPathFindUpdate(*wsc, 5s, /*requireAlts=*/false);
        BEAST_EXPECT(upd);
        if (upd)
        {
            BEAST_EXPECT(upd->isMember(jss::ledger_hash));
            BEAST_EXPECT(upd->isMember(jss::ledger_index));
            if (upd->isMember(jss::ledger_hash) && upd->isMember(jss::ledger_index))
            {
                auto const hash = (*upd)[jss::ledger_hash].asString();
                auto const idx = (*upd)[jss::ledger_index].asUInt();
                BEAST_EXPECT(hash == to_string(closed->header().hash));
                BEAST_EXPECT(idx == closed->seq());
                BEAST_EXPECT(idx != open->seq() || hash != to_string(open->header().hash));
            }
        }

        // Cache must stay on the closed view even if a caller asks to
        // advance authoritatively with the open ledger.
        auto cache = prm.getAssetCache(open, /*authoritative=*/true);
        BEAST_EXPECT(!cache->getLedger()->open());
        BEAST_EXPECT(cache->getLedger()->seq() == closed->seq());

        json::Value closeReq;
        closeReq[jss::subcommand] = "close";
        (void)wsc->invoke("path_find", closeReq);
    }

    /**
     * A create more than 8 ledgers ahead must not forceClear the AssetCache
     * instance an in-flight updateAll already captured. Replace the manager
     * pointer with a new cache; the held instance keeps its ledger and lines.
     */
    void
    testCreateLargeJumpDoesNotMutateInFlightCache()
    {
        testcase("create: large jump replaces cache; in-flight instance unchanged");
        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            cfg->workers = 4;
            cfg->pathMidCloseDelay = std::chrono::hours{1};
            return cfg;
        }));

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        setupUsdCorridor(env, gw, alice, bob);

        auto& prm = env.app().getPathRequestManager();
        auto const firstClosed = env.closed();
        auto held = prm.getAssetCache(firstClosed, /*authoritative=*/true);
        {
            AssetCache::SessionPin const pin{1};
            BEAST_EXPECT(held->getRippleLines(alice.id()));
        }
        auto const heldSeq = held->getLedger()->seq();
        auto const heldLines = held->totalLineCount();
        auto* const heldPtr = held.get();
        BEAST_EXPECT(heldLines >= 1);

        for (int i = 0; i < 10; ++i)
            env.close();
        auto const nowClosed = env.closed();
        BEAST_EXPECT(nowClosed->seq() > heldSeq + 8);

        // Same arguments as makePathRequest (authoritative=false).
        auto created = prm.getAssetCache(nowClosed, /*authoritative=*/false);
        BEAST_EXPECT(created.get() != heldPtr);
        BEAST_EXPECT(created->getLedger()->seq() == nowClosed->seq());
        BEAST_EXPECT(held->getLedger()->seq() == heldSeq);
        BEAST_EXPECT(held->totalLineCount() == heldLines);
        held->releaseSession(1);
    }

public:
    void
    run() override
    {
        testRevalidateAcrossCloses();
        testMultiSessionSharedCache();
        testSingleWorkerMultiSessionNoHang();
        testTwoWorkerMultiSessionNoHang();
        testSixPathShape();
        testPartialLiquidityNoCoveringSpare();
        testRankKeepsLookingForCoveringLiquidity();
        testStaggeredRediscoverySurvivesManyCloses();
        testMidCloseRevalidateOnly();
        testMidClosePreservesNewSubscriptionSignal();
        testAutoSourceKeepsXrpUnderSoftCap();
        testCreateRebuildsStaleSharedCache();
        testMidCloseDoesNotForceClearOpenCache();
        testCloseDuringUpdateReclaimsPins();
        testCreateWakeOnOpenReportsClosedIdentity();
        testCreateLargeJumpDoesNotMutateInFlightCache();
    }
};

BEAST_DEFINE_TESTSUITE(PathFindSub, rpc, xrpl);

}  // namespace xrpl::test
