// cspell:ignore ISTOGRAM Wreturn
#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/misc/SHAMapStoreSpanNames.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/State.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/shamap/TreeNodeCache.h>
#include <xrpl/telemetry/MetricMacros.h>
#include <xrpl/telemetry/MetricNames.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace xrpl {

class NetworkOPs;

class SHAMapStoreImp : public SHAMapStore
{
private:
    class SavedStateDB
    {
    public:
        soci::session sqlDb;
        std::mutex mutex;
        beast::Journal const journal;

        // Just instantiate without any logic in case online delete is not
        // configured
        explicit SavedStateDB() : journal{beast::Journal::getNullSink()}
        {
        }

        // opens database and, if necessary, creates & initializes its tables.
        void
        init(BasicConfig const& config, std::string const& dbName);
        // get/set the ledger index that we can delete up to and including
        LedgerIndex
        getCanDelete();
        LedgerIndex
        setCanDelete(LedgerIndex canDelete);
        SavedState
        getState();
        void
        setState(SavedState const& state);
        void
        setLastRotated(LedgerIndex seq);
    };

    Application& app_;

    // name of state database
    std::string const dbName_ = "state";
    // prefix of on-disk nodestore backend instances
    std::string const dbPrefix_ = "rippledb";  // cspell: disable-line
    // check health/stop status as records are copied
    std::uint64_t const checkHealthInterval_ = 1000;
    // minimum # of ledgers to maintain for health of network
    static std::uint32_t const kMinimumDeletionInterval = 256;
    // minimum # of ledgers required for standalone mode.
    static std::uint32_t const kMinimumDeletionIntervalSa = 8;
    // minimum ledger to maintain online.
    std::atomic<LedgerIndex> minimumOnline_;

    node_store::Scheduler& scheduler_;
    beast::Journal const journal_;
    node_store::DatabaseRotating* dbRotating_ = nullptr;
    SavedStateDB stateDb_;
    std::thread thread_;
    bool stop_ = false;
    bool healthy_ = true;
    // Used to prevent ledger gaps from forming during online deletion. Keeps
    // track of the last validated ledger that was processed without gaps. There
    // are no guarantees about gaps while online delete is not running. For
    // that, use advisory_delete and check for gaps externally.
    LedgerIndex lastGoodValidatedLedger_ = 0;
    // Used to prevent the circuit breaker from tripping too quickly.
    LedgerIndex lastSuccessfulHealthCheck_ = 0;
    mutable std::condition_variable cond_;
    mutable std::condition_variable rendezvous_;
    mutable std::mutex mutex_;
    std::shared_ptr<Ledger const> newLedger_;
    std::atomic<bool> working_;
    std::atomic<LedgerIndex> canDelete_;
    int fdRequired_ = 0;

    std::uint32_t deleteInterval_ = 0;
    bool advisoryDelete_ = false;
    std::uint32_t deleteBatch_ = 100;
    std::chrono::milliseconds backOff_{100};
    std::chrono::seconds ageThreshold_{60};
    /**
     * If the node is out of sync, or any recent ledgers are not
     * available during an online_delete healthWait() call, sleep
     * the thread for this time, and continue checking until recovery.
     * See also: "recovery_wait_seconds" in xrpld-example.cfg
     */
    std::chrono::seconds recoveryWaitTime_{2};
    /**
     * If the rotation stays "unhealthy" for a very long time, the process is aborted, and tried
     * again later. This value represents the number of ledgers that must be validated without
     * making rotation progress before the process is aborted.
     */
    std::uint32_t maxWaitingLedgers_ = deleteBatch_;

    // these do not exist upon SHAMapStore creation, but do exist
    // as of run() or before
    NetworkOPs* netOPs_ = nullptr;
    LedgerMaster* ledgerMaster_ = nullptr;
    FullBelowCache* fullBelowCache_ = nullptr;
    TreeNodeCache* treeNodeCache_ = nullptr;

    static constexpr auto kNodeStoreName = "NodeStore";

public:
    SHAMapStoreImp(Application& app, node_store::Scheduler& scheduler, beast::Journal journal);

    std::uint32_t
    clampFetchDepth(std::uint32_t fetchDepth) const override
    {
        return (deleteInterval_ != 0u) ? std::min(fetchDepth, deleteInterval_) : fetchDepth;
    }

    std::unique_ptr<node_store::Database>
    makeNodeStore(int readThreads) override;

    LedgerIndex
    setCanDelete(LedgerIndex seq) override
    {
        if (advisoryDelete_)
            canDelete_ = seq;
        return stateDb_.setCanDelete(seq);
    }

    bool
    advisoryDelete() const override
    {
        return advisoryDelete_;
    }

    // All ledgers prior to this one are eligible
    // for deletion in the next rotation
    LedgerIndex
    getLastRotated() override
    {
        return stateDb_.getState().lastRotated;
    }

    // All ledgers before and including this are unprotected
    // and online delete may delete them if appropriate
    LedgerIndex
    getCanDelete() override
    {
        return canDelete_;
    }

    void
    onLedgerClosed(std::shared_ptr<Ledger const> const& ledger) override;

    [[nodiscard]]
    bool
    rendezvous(std::optional<std::chrono::milliseconds> const& timeout = {}) const override;
    int
    fdRequired() const override;

    std::optional<LedgerIndex>
    minimumOnline() const override;

private:
    // callback for visitNodes
    bool
    copyNode(std::uint64_t& nodeCount, SHAMapTreeNode const& node);
    void
    run();
    void
    dbPaths();

    std::unique_ptr<node_store::Backend>
    makeBackendRotating(std::string path = std::string());

    /**
     * One rotation phase: a child span of nodestore.rotate for its lifetime
     * and one rotation_phase_duration_seconds record when it ends. Lives on
     * the SHAMapStore thread only. Not movable: hold it in a scope.
     *
     * `cache` names the cache a freshen phase worked on (lval::freshen_cache)
     * and is empty for every other phase.
     */
    class RotationPhase
    {
    public:
        template <std::size_t N>
        RotationPhase(
            SHAMapStoreImp& owner,
            telemetry::StaticStr<N> const& phase,
            std::string_view stage,
            std::string_view cache = {})
            : owner_(owner)
            , stage_(stage)
            , cache_(cache)
            , span_(telemetry::TraceCategory::Ledger, telemetry::nodestore_span::rotateFull, phase)
        {
            if (!cache_.empty())
                span_.setAttribute(telemetry::nodestore_span::attr::cache, cache_);
        }

        ~RotationPhase()
        {
            // [[maybe_unused]] so a -DXRPL_ENABLE_TELEMETRY=0 build (macro
            // expands to `do {} while (false)`) keeps compiling under -Werror.
            [[maybe_unused]] auto const seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
            // One label set for every phase, so this stays one histogram
            // instrument. `cache` is empty on the non-freshen phases, and
            // Prometheus treats an empty label as absent.
            XRPL_METRIC_HISTOGRAM_RECORD_LABELED(
                owner_.app_,
                telemetry::metric::rotationPhaseDurationSeconds,
                "Wall-clock seconds spent in one online-delete rotation phase",
                seconds,
                {{telemetry::label::stage, stage_}, {telemetry::label::cache, cache_}});
        }

        RotationPhase(RotationPhase const&) = delete;
        RotationPhase&
        operator=(RotationPhase const&) = delete;
        RotationPhase(RotationPhase&&) = delete;
        RotationPhase&
        operator=(RotationPhase&&) = delete;

        template <class Value>
        void
        setAttribute(std::string_view key, Value value) noexcept
        {
            span_.setAttribute(key, value);
        }

    private:
        // The three below are read only inside the metric macro in the
        // destructor, so a -DXRPL_ENABLE_TELEMETRY=0 build sees no use at all.
        [[maybe_unused]] SHAMapStoreImp& owner_;
        // Owned copies: the constructor takes views so callers can pass the
        // label constants, but a view stored in a member would only be valid
        // as long as the caller's text was. A phase is built a handful of
        // times per rotation, so two small strings cost nothing.
        [[maybe_unused]] std::string const stage_;
        [[maybe_unused]] std::string const cache_;
        std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
        telemetry::ScopedSpanGuard span_;
    };

    // True while run() is inside its rotation block. Read and written on the
    // SHAMapStore thread only, so it needs no lock.
    bool rotating_ = false;

    /**
     * Re-fetch every key of one cache so any node only the archive still
     * holds is copied into the writable backend before the archive is deleted.
     *
     * @param cache The cache to walk.
     * @param cacheName Its lval::freshen_cache label value.
     * @return true if healthWait() said the rotation must stop or expire.
     */
    template <class CacheInstance>
    bool
    freshenCache(CacheInstance& cache, std::string_view cacheName)
    {
        namespace ns = telemetry::nodestore_span;
        namespace lv = telemetry::lval::rotation_phase;

        RotationPhase phase(*this, ns::phase::freshenFetch, lv::freshenFetch, cacheName);
        auto const copiedBefore = dbRotating_->duplicateCopyForwardTotal();

        // Keys are copied one map partition at a time, and each fetch runs with
        // the cache mutex released. getKeys() held the mutex across the whole
        // cache, which on a large tree-node cache stalls every job for seconds
        // and can drop the node out of sync. Once healthWait() says stop, the
        // remaining partitions are still copied (cheap) but no longer fetched.
        std::uint64_t fetched = 0;
        bool stop = false;
        cache.forEachKeyPartition([&](auto const& keys) {
            if (stop)
                return;
            for (auto const& key : keys)
            {
                dbRotating_->fetchNodeObject(key, 0, node_store::FetchType::Synchronous, true);
                if (!(++fetched % checkHealthInterval_) && healthWait() != HealthResult::KeepGoing)
                {
                    stop = true;
                    return;
                }
            }
        });
        auto const copied = dbRotating_->duplicateCopyForwardTotal() - copiedBefore;
        phase.setAttribute(ns::attr::keyCount, static_cast<std::int64_t>(fetched));
        phase.setAttribute(ns::attr::keysCopied, static_cast<std::int64_t>(copied));
        recordFreshen(cacheName, fetched, copied);

        return stop;
    }

    /**
     * Count one finished freshen on rotation_freshen_keys_total and log it.
     * Kept out of the template above so the metric instrument is created once,
     * not once per cache type.
     */
    void
    recordFreshen(std::string_view cacheName, std::uint64_t fetched, std::uint64_t copied);

    /**
     * delete from sqlite table in batches to not lock the db excessively.
     *  Pause briefly to extend access time to other users.
     *  Call with mutex object unlocked.
     */
    void
    clearSql(
        LedgerIndex lastRotated,
        std::string const& tableName,
        std::function<std::optional<LedgerIndex>()> const& getMinSeq,
        std::function<void(LedgerIndex)> const& deleteBeforeSeq);
    void
    clearCaches(LedgerIndex validatedSeq);
    void
    freshenCaches();
    void
    clearPrior(LedgerIndex lastRotated);

    /**
     * This is a health check for online deletion that waits until xrpld is
     * stable before returning. It returns an indication of whether the server
     * is stopping, or if this attempt should be abandoned.
     *
     * @return Whether the server is stopping.
     */
    enum class HealthResult { Stopping, Expired, KeepGoing };
    [[nodiscard]] HealthResult
    healthWait();

public:
    void
    start() override
    {
        if (deleteInterval_ != 0u)
            thread_ = std::thread(&SHAMapStoreImp::run, this);
    }

    void
    stop() override;
};

}  // namespace xrpl
