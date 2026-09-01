#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/NodeStoreScheduler.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

class SHAMapStore_test : public beast::unit_test::Suite
{
    static auto const kDeleteInterval = 8;

    // Mirrors SHAMapStoreImp::kMinimumDeletionIntervalSa, the floor that
    // online_delete is held to in standalone mode. Note that the
    // max_waiting_ledgers floor is derived from this minimum rather than from
    // the configured interval, so both constants below stay put if
    // kDeleteInterval is ever raised.
    static auto const kMinDeleteInterval = 8;
    static auto const kMinWaitingLedgers = kMinDeleteInterval / 4;
    static_assert(kDeleteInterval >= kMinDeleteInterval);

    // The two wait durations healthWait() can choose from, spelled as they
    // appear in its log message. onlineDelete() below sets
    // recovery_wait_seconds to 1, so the full wait is 1000ms and the shortened
    // wait is a tenth of that. Note that "Waiting 1000ms" does not contain
    // "Waiting 100ms", so the two are distinguishable by substring.
    static constexpr char const* kFullWait = "Waiting 1000ms for node to stabilize";
    static constexpr char const* kShortWait = "Waiting 100ms for node to stabilize";

    // Distinctive fragments of the other messages the tests below key off. Each
    // is unique among everything SHAMapStoreImp logs, so a substring match
    // identifies the message unambiguously.
    //
    // kRotating is logged once run() has committed to a rotation, immediately
    // after the health check that gates it, and kFinished once a rotation has
    // run to completion. kExpired is logged by healthWait() when the circuit
    // breaker trips.
    static constexpr char const* kRotating = "rotating";
    static constexpr char const* kFinished = "finished rotation";
    static constexpr char const* kExpired = "unable to make progress";

    // A Logs implementation that records every message the store's own
    // partition emits, keeping each message's severity alongside its text, and
    // lets a test block until a given message has appeared.
    //
    // Severity is recorded because healthWait() picks the severity and the wait
    // duration together, so the pair identifies which of its three logging
    // branches ran: warn at the full wait when the server is unhealthy for a
    // reason that is not expected to resolve on its own, trace at a tenth of
    // the wait when the only missing ledger is the one currently being built,
    // and info at the full wait otherwise. Matching on the pair is what lets
    // the tests below assert which branch was taken instead of merely that some
    // wait happened.
    //
    // waitFor() exists because run() logs on entry to a rotation, which is the
    // only signal a test has that the store has passed the health check gating
    // the rotation and is now inside it. Several of the branches under test are
    // only reachable from there, and no other handle on the store exposes it.
    class StoreLogs : public Logs
    {
        mutable std::mutex mutex_;
        std::condition_variable cond_;
        std::vector<std::pair<beast::Severity, std::string>> messages_;

        class Sink : public beast::Journal::Sink
        {
            StoreLogs& owner_;

        public:
            Sink(beast::Severity threshold, StoreLogs& owner)
                : beast::Journal::Sink(threshold, false), owner_(owner)
            {
            }

            // Env::AppBundle calls Logs::threshold() after the Application is
            // built, which would otherwise raise this sink above Trace and
            // discard the messages the buildingIndex branch logs.
            void
            threshold(beast::Severity) override
            {
            }

            void
            write(beast::Severity level, std::string const& text) override
            {
                {
                    std::scoped_lock const lock(owner_.mutex_);
                    owner_.messages_.emplace_back(level, text);
                }
                owner_.cond_.notify_all();
            }

            void
            writeAlways(beast::Severity level, std::string const& text) override
            {
                write(level, text);
            }
        };

        // Caller must hold mutex_. A nullopt severity matches any severity.
        [[nodiscard]] std::size_t
        countLocked(std::optional<beast::Severity> severity, std::string const& text) const
        {
            return std::count_if(messages_.begin(), messages_.end(), [&](auto const& message) {
                return (!severity || message.first == *severity) && message.second.contains(text);
            });
        }

    public:
        StoreLogs() : Logs(beast::Severity::Trace)
        {
        }

        // Only the store's own partition is logged at Trace; everything else
        // is silenced, so that enabling trace for this one branch does not pay
        // for formatting every trace message in the server.
        std::unique_ptr<beast::Journal::Sink>
        makeSink(std::string const& partition, beast::Severity) override
        {
            return std::make_unique<Sink>(
                partition == "SHAMapStore" ? beast::Severity::Trace : beast::Severity::Disabled,
                *this);
        }

        // How many recorded messages were logged at `severity` and contain
        // `text`.
        [[nodiscard]] std::size_t
        count(beast::Severity severity, std::string const& text) const
        {
            std::scoped_lock const lock(mutex_);
            return countLocked(severity, text);
        }

        // How many recorded messages contain `text`, at any severity.
        [[nodiscard]] std::size_t
        count(std::string const& text) const
        {
            std::scoped_lock const lock(mutex_);
            return countLocked(std::nullopt, text);
        }

        // Blocks until `text` has been logged at least `expected` times in
        // total, or until the timeout expires. Returns whether it got there.
        [[nodiscard]] bool
        waitFor(
            std::string const& text,
            std::chrono::milliseconds timeout,
            std::size_t expected = 1)
        {
            std::unique_lock lock(mutex_);
            return cond_.wait_for(
                lock, timeout, [&] { return countLocked(std::nullopt, text) >= expected; });
        }
    };

    static auto
    onlineDelete(std::unique_ptr<Config> cfg)
    {
        cfg = jtx::onlineDelete(std::move(cfg), kDeleteInterval);
        cfg->section(Sections::kNodeDatabase).set(Keys::kRecoveryWaitSeconds, "1");
        return cfg;
    }

    // online delete tuned so that a rotation, once it has started, spends a
    // long time in clearPrior() before reaching the first health check inside
    // the rotation body.
    //
    // clearSql() sleeps back_off_milliseconds at the top of every iteration and
    // advances by delete_batch rows per iteration, so a delete_batch of 1 costs
    // one sleep per ledger removed, for each of the three tables it is called
    // on. That is what gives parkMidRotation() below a window measured in
    // seconds rather than in microseconds.
    //
    // max_waiting_ledgers is pinned to its floor so that tripping the circuit
    // breaker takes the fewest possible ledger closes.
    static auto
    slowOnlineDelete(std::unique_ptr<Config> cfg)
    {
        cfg = onlineDelete(std::move(cfg));
        auto& section = cfg->section(Sections::kNodeDatabase);
        section.set(Keys::kDeleteBatch, "1");
        section.set(Keys::kBackOffMilliseconds, "100");
        section.set(Keys::kMaxWaitingLedgers, std::to_string(kMinWaitingLedgers));
        return cfg;
    }

    static auto
    advisoryDelete(std::unique_ptr<Config> cfg)
    {
        cfg = onlineDelete(std::move(cfg));
        cfg->section(Sections::kNodeDatabase).set(Keys::kAdvisoryDelete, "1");
        return cfg;
    }

    static bool
    goodLedger(jtx::Env& env, json::Value const& json, std::string ledgerID, bool checkDB = false)
    {
        auto good = json.isMember(jss::result) && !rpc::containsError(json[jss::result]) &&
            json[jss::result][jss::ledger][jss::ledger_index] == ledgerID;
        if (!good || !checkDB)
            return good;

        auto const seq = json[jss::result][jss::ledger_index].asUInt();

        std::optional<LedgerHeader> outInfo =
            env.app().getRelationalDatabase().getLedgerInfoByIndex(seq);
        if (!outInfo)
            return false;
        LedgerHeader const& info = outInfo.value();

        std::string const outHash = to_string(info.hash);
        LedgerIndex const outSeq = info.seq;
        std::string const outParentHash = to_string(info.parentHash);
        std::string const outDrops = to_string(info.drops);
        std::uint64_t const outCloseTime = info.closeTime.time_since_epoch().count();
        std::uint64_t const outParentCloseTime = info.parentCloseTime.time_since_epoch().count();
        std::uint64_t const outCloseTimeResolution = info.closeTimeResolution.count();
        std::uint64_t const outCloseFlags = info.closeFlags;
        std::string const outAccountHash = to_string(info.accountHash);
        std::string const outTxHash = to_string(info.txHash);

        auto const& ledger = json[jss::result][jss::ledger];
        return outHash == ledger[jss::ledger_hash].asString() && outSeq == seq &&
            outParentHash == ledger[jss::parent_hash].asString() &&
            outDrops == ledger[jss::total_coins].asString() &&
            outCloseTime == ledger[jss::close_time].asUInt() &&
            outParentCloseTime == ledger[jss::parent_close_time].asUInt() &&
            outCloseTimeResolution == ledger[jss::close_time_resolution].asUInt() &&
            outCloseFlags == ledger[jss::close_flags].asUInt() &&
            outAccountHash == ledger[jss::account_hash].asString() &&
            outTxHash == ledger[jss::transaction_hash].asString();
    }

    static bool
    bad(json::Value const& json, ErrorCodeI error = RpcLgrNotFound)
    {
        return json.isMember(jss::result) && rpc::containsError(json[jss::result]) &&
            json[jss::result][jss::error_code] == error;
    }

    std::string
    getHash(json::Value const& json)
    {
        BEAST_EXPECT(
            json.isMember(jss::result) && json[jss::result].isMember(jss::ledger) &&
            json[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
            json[jss::result][jss::ledger][jss::ledger_hash].isString());
        return json[jss::result][jss::ledger][jss::ledger_hash].asString();
    }

    void
    ledgerCheck(jtx::Env& env, int const rows, int const first)
    {
        auto const [actualRows, actualFirst, actualLast] =
            env.app().getRelationalDatabase().getLedgerCountMinMax();

        BEAST_EXPECT(actualRows == rows);
        BEAST_EXPECT(actualFirst == first);
        BEAST_EXPECT(actualLast == first + rows - 1);
    }

    void
    transactionCheck(jtx::Env& env, int const rows)
    {
        BEAST_EXPECT(env.app().getRelationalDatabase().getTransactionCount() == rows);
    }

    void
    accountTransactionCheck(jtx::Env& env, int const rows)
    {
        BEAST_EXPECT(env.app().getRelationalDatabase().getAccountTransactionCount() == rows);
    }

    // Wait until the SHAMapStore has finished processing the ledger that the
    // preceding env.close() produced.
    //
    // env.close() returns as soon as the ledger_accept RPC returns, but the
    // validated ledger path -- LedgerMaster::setValidLedger() ->
    // SHAMapStore::onLedgerClosed() -- runs on a job queue thread. Without
    // draining the job queue first, the store may not have been handed the
    // ledger at all, in which case rendezvous() observes working_ == false and
    // returns immediately, before any work has been done.
    [[nodiscard]] static bool
    syncStore(jtx::Env& env)
    {
        env.app().getJobQueue().rendezvous();
        // Use the timeout overload so that a store which never finishes fails
        // this test rather than hanging the entire unit test job.
        return env.app().getSHAMapStore().rendezvous(std::chrono::seconds{60});
    }

    int
    waitForReady(jtx::Env& env)
    {
        using namespace std::chrono_literals;

        auto& store = env.app().getSHAMapStore();

        int ledgerSeq = 3;
        BEAST_EXPECT(syncStore(env));
        BEAST_EXPECT(!store.getLastRotated());

        env.close();
        BEAST_EXPECT(syncStore(env));

        auto ledger = env.rpc("ledger", "validated");
        BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++)));

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        return ledgerSeq;
    }

    // Construct an Env whose config has online_delete enabled and is then
    // mutated by `tweak`, and report how SHAMapStoreImp's constructor judged
    // it: the message of the exception it threw, or std::nullopt if the Env was
    // constructed successfully.
    //
    // SHAMapStoreImp is built from ApplicationImp's member initializer list, so
    // a configuration it rejects surfaces as an exception thrown out of the Env
    // constructor rather than as a failure at some later point.
    //
    // Note that ~AppBundle does not run when the Env constructor throws, so the
    // global debug log sink it installed -- which holds a reference to this
    // suite -- is left in place until the next Env is constructed. The caller
    // must therefore end with a configuration that is accepted, so that a
    // successful ~AppBundle clears the sink before the suite goes out of scope.
    std::optional<std::string>
    storeConfigResult(std::function<void(Config&)> const& tweak)
    {
        using namespace test::jtx;

        try
        {
            Env const env{
                *this,
                envconfig([&tweak](std::unique_ptr<Config> cfg) {
                    cfg = onlineDelete(std::move(cfg));
                    tweak(*cfg);
                    return cfg;
                }),
                nullptr,
                beast::Severity::Disabled};
            return std::nullopt;
        }
        // Deliberately broader than the std::runtime_error that
        // SHAMapStoreImp throws: an unexpected exception type then shows up as
        // a message mismatch naming the actual failure, rather than escaping
        // this testcase.
        catch (std::exception const& e)
        {
            return std::string{e.what()};
        }
    }

    void
    expectConfigRejected(std::string const& expected, std::function<void(Config&)> const& tweak)
    {
        auto const result = storeConfigResult(tweak);
        BEAST_EXPECTS(result == expected, result.value_or("<accepted>"));
    }

    void
    expectConfigAccepted(std::function<void(Config&)> const& tweak)
    {
        auto const result = storeConfigResult(tweak);
        BEAST_EXPECTS(!result, result.value_or(""));
    }

    // The state parkInHealthWait() leaves behind.
    struct Parked
    {
        // Value of getLastRotated() before the rotation attempt began. The
        // store must still report this for as long as it stays parked.
        LedgerIndex lastRotated = 0;
        // The validated ledger the store is waiting on, and the sequence
        // getLastRotated() will report once the rotation finally completes.
        LedgerIndex validated = 0;
        // Sequence removed from LedgerMaster to create the gap, or 0 if
        // `createGap` was false.
        LedgerIndex gap = 0;
    };

    // Drive the store to the point where it is parked inside healthWait(),
    // unable to proceed with a rotation: close ledgers until one more close
    // would make the store due to rotate, optionally remove the newest ledger
    // from LedgerMaster so that the attempt sees a gap in the range, close the
    // triggering ledger, and then set the operating mode to `modeAfterClose`.
    //
    // Once parked, the store stays parked indefinitely. Its wait loop reruns
    // every recovery_wait_seconds and exits only when the server looks healthy,
    // when it is stopped, or when the validated ledger index reaches the
    // circuit breaker -- and that index only advances when this test closes
    // another ledger. So a caller can establish any server state it likes,
    // hold it, and be sure the store observes it. That is what makes the tests
    // below state machines rather than races.
    //
    // Returns std::nullopt if the setup did not reach a parked store, having
    // already reported the failure.
    std::optional<Parked>
    parkInHealthWait(jtx::Env& env, bool createGap, OperatingMode modeAfterClose)
    {
        using namespace std::chrono_literals;
        using namespace test::jtx;

        auto& lm = env.app().getLedgerMaster();
        auto& store = env.app().getSHAMapStore();
        auto& netOPs = env.app().getOPs();

        env.fund(XRP(1000), Account("alice"));
        env.close();
        if (!BEAST_EXPECT(syncStore(env)))
            return std::nullopt;

        Parked parked;
        // The store adopts the first validated ledger it sees as lastRotated,
        // and which one that is depends on timing, so read it rather than
        // assuming a value.
        parked.lastRotated = store.getLastRotated();
        if (!BEAST_EXPECT(parked.lastRotated))
            return std::nullopt;

        // Close ledgers until the next close is the one that makes
        // validatedSeq reach lastRotated + deleteInterval.
        LedgerIndex maxSeq = env.closed()->header().seq;
        while (maxSeq + 1 < parked.lastRotated + kDeleteInterval)
        {
            env.close();
            ++maxSeq;
            if (!BEAST_EXPECT(syncStore(env)))
                return std::nullopt;
            if (!BEAST_EXPECTS(
                    store.getLastRotated() == parked.lastRotated,
                    std::to_string(store.getLastRotated())))
                return std::nullopt;
        }

        // Drop out of FULL before touching LedgerMaster's internals, matching
        // testLedgerGaps. This also keeps the store from rotating on the
        // triggering close before the caller has set the state it wants
        // observed.
        netOPs.setMode(OperatingMode::CONNECTED);

        if (createGap)
        {
            std::size_t iterations = 30;
            while (!lm.haveLedger(maxSeq) && --iterations > 0)
            {
                std::this_thread::sleep_for(10ms);
            }
            if (!BEAST_EXPECTS(lm.haveLedger(maxSeq), std::to_string(maxSeq)))
                return std::nullopt;

            // Give the server a moment to finish any internal work on the
            // ledger about to be removed, as testLedgerGaps does.
            std::this_thread::sleep_for(250ms);

            lm.clearLedger(maxSeq);
            if (!BEAST_EXPECT(!lm.haveLedger(maxSeq)))
                return std::nullopt;
            parked.gap = maxSeq;
        }

        // This close makes the store due to rotate.
        env.close();
        ++maxSeq;
        parked.validated = maxSeq;
        netOPs.setMode(modeAfterClose);

        // Drain the job queue so that onLedgerClosed() has handed the ledger to
        // the store. Without this, working_ may still be false from the
        // previous cycle and rendezvous() would report "done" before the store
        // has even looked at this ledger.
        env.app().getJobQueue().rendezvous();

        if (!BEAST_EXPECT(!store.rendezvous(1s)))
            return std::nullopt;
        if (!BEAST_EXPECTS(
                store.getLastRotated() == parked.lastRotated,
                std::to_string(store.getLastRotated())))
            return std::nullopt;

        return parked;
    }

    // Drive the store to the point where it has passed the health check that
    // gates a rotation and is inside the rotation body, then make the server
    // unhealthy so that the next health check in there parks it.
    //
    // The gap cannot be created up front the way parkInHealthWait() does it,
    // because the gating check would see it and refuse to start the rotation at
    // all -- which is what testLedgerGaps() exercises. So this waits for the
    // message run() logs immediately after that check, which is the store
    // publishing that it is committed to the rotation, and creates the gap then.
    //
    // The margin that makes that safe is clearPrior(), which runs between the
    // log and the first health check inside the rotation. Under
    // slowOnlineDelete() it works through three tables one sequence at a time,
    // sleeping back_off_milliseconds before each, and checks health after every
    // one of those sleeps. So the store spends on the order of a second per
    // table repeatedly asking whether it is healthy, against the microseconds
    // this function needs to clear a ledger once waitFor() has returned.
    //
    // The gap has to be the validated ledger itself. healthWait() counts missing
    // ledgers over the range from lastGoodValidatedLedger_ to the validated
    // index, and run() sets the former to the latter just before starting the
    // rotation, so for the duration of the rotation that range begins as a
    // single sequence and grows only as the caller closes more ledgers.
    //
    // Which health check inside the rotation ends up observing the gap is not
    // pinned down, and does not need to be: whichever one it is returns the same
    // answer, clearPrior() gives up, and run() reaches its first switch on
    // healthWait() with the condition still in force. Every assertion below
    // holds for any of them.
    //
    // Returns std::nullopt if the setup did not reach a parked store, having
    // already reported the failure.
    std::optional<Parked>
    parkMidRotation(jtx::Env& env, StoreLogs& log)
    {
        using namespace std::chrono_literals;
        using namespace test::jtx;

        auto& lm = env.app().getLedgerMaster();
        auto& store = env.app().getSHAMapStore();

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();
        if (!BEAST_EXPECT(syncStore(env)))
            return std::nullopt;

        LedgerIndex maxSeq = env.closed()->header().seq;
        // Close one ledger, carrying a transaction so that the sequence has rows
        // in all three of the tables clearSql() works through.
        auto closeOne = [&]() -> bool {
            env(noop(alice));
            env.close();
            ++maxSeq;
            return BEAST_EXPECT(syncStore(env));
        };

        // Let one rotation complete before setting up the one to be parked. The
        // window this helper depends on only exists once the tables hold a full
        // delete interval of rows: on the very first rotation there is at most
        // one sequence to remove, so clearSql() sleeps once or not at all.
        LedgerIndex const firstRotated = store.getLastRotated();
        if (!BEAST_EXPECT(firstRotated))
            return std::nullopt;
        while (store.getLastRotated() == firstRotated)
        {
            if (!closeOne())
                return std::nullopt;
            // The rotation is due once maxSeq reaches firstRotated +
            // kDeleteInterval. Allow one close beyond that before giving up,
            // rather than closing ledgers forever.
            if (!BEAST_EXPECTS(maxSeq <= firstRotated + kDeleteInterval, std::to_string(maxSeq)))
                return std::nullopt;
        }

        Parked parked;
        parked.lastRotated = store.getLastRotated();

        // Close ledgers until the next close is the one that makes the store due
        // to rotate again.
        while (maxSeq + 1 < parked.lastRotated + kDeleteInterval)
        {
            if (!closeOne())
                return std::nullopt;
            if (!BEAST_EXPECTS(
                    store.getLastRotated() == parked.lastRotated,
                    std::to_string(store.getLastRotated())))
                return std::nullopt;
        }

        // One rotation has already been logged, so wait for the next one rather
        // than for the first.
        auto const rotationsBefore = log.count(kRotating);

        // This close makes the store due to rotate. Deliberately do not drain
        // the store here: the point is to interrupt it partway through.
        env(noop(alice));
        env.close();
        ++maxSeq;
        parked.validated = maxSeq;

        // Draining the job queue, on the other hand, is required. In standalone
        // mode switchLCL() inserts the closed ledger into LedgerMaster's
        // complete range and then posts an advance job, and publishing the
        // ledger from that job inserts it a second time. Clearing the ledger
        // between those two inserts does not leave a lasting gap: publication
        // puts it straight back, the rotation's health checks see a healthy
        // node, and the rotation runs to completion. Waiting for the queue to
        // drain closes that window, because publication is what advances
        // pubLedger_ -- once it has happened, the ledger is never published, and
        // so never inserted, again.
        //
        // This waits only for the job queue, not for the store, whose thread is
        // its own and is the thing being interrupted here.
        env.app().getJobQueue().rendezvous();

        // Publishing the ledger is what advances pubLedger_, so this is the
        // observable confirmation that the window above has closed. Asserting it
        // here means that if anything ever reopens it, this setup step says so
        // directly instead of the tests below failing for reasons that look
        // nothing like the cause.
        auto const published = lm.getPublishedLedger();
        if (!BEAST_EXPECTS(
                published && published->header().seq >= parked.validated,
                std::to_string(published ? published->header().seq : 0)))
            return std::nullopt;

        if (!BEAST_EXPECT(log.waitFor(kRotating, 10s, rotationsBefore + 1)))
            return std::nullopt;

        // The store is now inside clearPrior(). Remove the validated ledger so
        // that every health check from here on reports a gap.
        if (!BEAST_EXPECTS(lm.haveLedger(parked.validated), std::to_string(parked.validated)))
            return std::nullopt;
        lm.clearLedger(parked.validated);
        parked.gap = parked.validated;
        if (!BEAST_EXPECT(!lm.haveLedger(parked.gap)))
            return std::nullopt;

        // The rotation must now be stuck. Wait longer than
        // recovery_wait_seconds, so that this is a settled state rather than a
        // store that has yet to reach its next health check.
        if (!BEAST_EXPECT(!store.rendezvous(1500ms)))
            return std::nullopt;
        if (!BEAST_EXPECTS(
                store.getLastRotated() == parked.lastRotated,
                std::to_string(store.getLastRotated())))
            return std::nullopt;
        // The rotation started, has not finished, and has not yet given up.
        if (!BEAST_EXPECTS(
                log.count(kRotating) == rotationsBefore + 1, std::to_string(log.count(kRotating))))
            return std::nullopt;
        if (!BEAST_EXPECTS(log.count(kFinished) == 1, std::to_string(log.count(kFinished))))
            return std::nullopt;
        if (!BEAST_EXPECTS(log.count(kExpired) == 0, std::to_string(log.count(kExpired))))
            return std::nullopt;

        return parked;
    }

public:
    // Cover the [node_db] validation that SHAMapStoreImp performs when it is
    // constructed. The rejected cases stop inside SHAMapStoreImp's constructor,
    // so they cost only a partial Application construction; the accepted ones
    // start a full node and immediately tear it down.
    void
    testConfig()
    {
        testcase("config validation");

        // online_delete below the standalone minimum. ledger_history is still
        // kDeleteInterval here, so it is too large for this online_delete as
        // well; the assertion pins which of the two errors wins.
        expectConfigRejected(
            "online_delete must be at least " + std::to_string(kMinDeleteInterval),
            [](Config& cfg) {
                cfg.section(Sections::kNodeDatabase)
                    .set(Keys::kOnlineDelete, std::to_string(kMinDeleteInterval - 1));
            });

        // ledger_history above online_delete asks the node to retain more
        // history than online delete is allowed to keep.
        expectConfigRejected(
            "online_delete must not be less than ledger_history (currently " +
                std::to_string(kDeleteInterval + 1) + ")",
            [](Config& cfg) { cfg.ledgerHistory = kDeleteInterval + 1; });

        // recovery_wait_seconds is the interval at which online delete rechecks
        // the node's health while it waits for missing ledgers to arrive, so a
        // zero wait would turn that into a spin.
        expectConfigRejected("recovery_wait_seconds must be at least 1 second", [](Config& cfg) {
            cfg.section(Sections::kNodeDatabase).set(Keys::kRecoveryWaitSeconds, "0");
        });

        // max_waiting_ledgers is the circuit breaker that eventually lets
        // online delete stop waiting, so it has a floor rather than being
        // free-form.
        auto const tooFewWaiting =
            "max_waiting_ledgers must be at least " + std::to_string(kMinWaitingLedgers);
        expectConfigRejected(tooFewWaiting, [](Config& cfg) {
            cfg.section(Sections::kNodeDatabase)
                .set(Keys::kMaxWaitingLedgers, std::to_string(kMinWaitingLedgers - 1));
        });
        // 0 is not a magic "never give up" value, just a value below the floor.
        expectConfigRejected(tooFewWaiting, [](Config& cfg) {
            cfg.section(Sections::kNodeDatabase).set(Keys::kMaxWaitingLedgers, "0");
        });

        // The floor itself is accepted, and so is a value far above
        // online_delete: there is no upper bound.
        expectConfigAccepted([](Config& cfg) {
            cfg.section(Sections::kNodeDatabase)
                .set(Keys::kMaxWaitingLedgers, std::to_string(kMinWaitingLedgers));
        });
        expectConfigAccepted([](Config& cfg) {
            cfg.section(Sections::kNodeDatabase)
                .set(Keys::kMaxWaitingLedgers, std::to_string(kDeleteInterval * 100));
        });

        // All of the above is gated on online_delete being enabled. With it
        // turned off, the same values are ignored rather than rejected.
        expectConfigAccepted([](Config& cfg) {
            auto& section = cfg.section(Sections::kNodeDatabase);
            section.set(Keys::kOnlineDelete, "0");
            section.set(Keys::kMaxWaitingLedgers, "0");
            section.set(Keys::kRecoveryWaitSeconds, "0");
        });
    }

    void
    testClear()
    {
        using namespace std::chrono_literals;

        testcase("clearPrior");
        using namespace jtx;

        Env env(*this, envconfig(onlineDelete));

        auto& store = env.app().getSHAMapStore();
        env.fund(XRP(10000), noripple("alice"));

        ledgerCheck(env, 1, 2);
        transactionCheck(env, 0);
        accountTransactionCheck(env, 0);

        std::map<std::uint32_t, json::Value const> ledgers;

        auto ledgerTmp = env.rpc("ledger", "0");
        BEAST_EXPECT(bad(ledgerTmp));

        ledgers.emplace(1, env.rpc("ledger", "1"));
        BEAST_EXPECT(goodLedger(env, ledgers[1], "1"));

        ledgers.emplace(2, env.rpc("ledger", "2"));
        BEAST_EXPECT(goodLedger(env, ledgers[2], "2"));

        ledgerTmp = env.rpc("ledger", "current");
        BEAST_EXPECT(goodLedger(env, ledgerTmp, "3"));

        ledgerTmp = env.rpc("ledger", "4");
        BEAST_EXPECT(bad(ledgerTmp));

        ledgerTmp = env.rpc("ledger", "100");
        BEAST_EXPECT(bad(ledgerTmp));

        auto const firstSeq = waitForReady(env);
        auto lastRotated = firstSeq - 1;

        for (auto i = firstSeq + 1; i < kDeleteInterval + firstSeq; ++i)
        {
            env.fund(XRP(10000), noripple("test" + std::to_string(i)));
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, std::to_string(i)));
        }
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        for (auto i = 3; i < kDeleteInterval + lastRotated; ++i)
        {
            ledgers.emplace(i, env.rpc("ledger", std::to_string(i)));
            BEAST_EXPECT(
                goodLedger(env, ledgers[i], std::to_string(i), true) &&
                !getHash(ledgers[i]).empty());
        }

        ledgerCheck(env, kDeleteInterval + 1, 2);
        transactionCheck(env, kDeleteInterval);
        accountTransactionCheck(env, 2 * kDeleteInterval);

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(kDeleteInterval + 4)));
        }

        BEAST_EXPECT(syncStore(env));

        BEAST_EXPECT(store.getLastRotated() == kDeleteInterval + 3);
        lastRotated = store.getLastRotated();
        BEAST_EXPECT(lastRotated == 11);

        // That took care of the fake hashes
        ledgerCheck(env, kDeleteInterval + 1, 3);
        transactionCheck(env, kDeleteInterval);
        accountTransactionCheck(env, 2 * kDeleteInterval);

        // The last iteration of this loop should trigger a rotate
        for (auto i = lastRotated - 1; i < lastRotated + kDeleteInterval - 1; ++i)
        {
            env.close();

            ledgerTmp = env.rpc("ledger", "current");
            BEAST_EXPECT(goodLedger(env, ledgerTmp, std::to_string(i + 3)));

            ledgers.emplace(i, env.rpc("ledger", std::to_string(i)));
            BEAST_EXPECT(
                store.getLastRotated() == lastRotated || i == lastRotated + kDeleteInterval - 2);
            BEAST_EXPECT(
                goodLedger(env, ledgers[i], std::to_string(i), true) &&
                !getHash(ledgers[i]).empty());
        }

        BEAST_EXPECT(syncStore(env));

        BEAST_EXPECT(store.getLastRotated() == kDeleteInterval + lastRotated);

        ledgerCheck(env, kDeleteInterval + 1, lastRotated);
        transactionCheck(env, 0);
        accountTransactionCheck(env, 0);
    }

    void
    testAutomatic()
    {
        testcase("automatic online_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, envconfig(onlineDelete));
        auto& store = env.app().getSHAMapStore();

        auto ledgerSeq = waitForReady(env);
        auto lastRotated = ledgerSeq - 1;
        BEAST_EXPECT(store.getLastRotated() == lastRotated);
        BEAST_EXPECT(lastRotated != 2);

        // Because advisory_delete is unset,
        // "can_delete" is disabled.
        auto const canDelete = env.rpc("can_delete");
        BEAST_EXPECT(bad(canDelete, RpcNotEnabled));

        // Close ledgers without triggering a rotate
        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(syncStore(env));

        // The database will always have back to ledger 2,
        // regardless of lastRotated.
        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        {
            // Closing one more ledger triggers a rotate
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());

        lastRotated = store.getLastRotated();

        // Close enough ledgers to trigger another rotate
        for (; ledgerSeq < lastRotated + kDeleteInterval + 1; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, kDeleteInterval + 1, lastRotated);
        BEAST_EXPECT(lastRotated != store.getLastRotated());
    }

    void
    testCanDelete()
    {
        testcase("online_delete with advisory_delete");
        using namespace jtx;
        using namespace std::chrono_literals;

        // Same config with advisory_delete enabled
        Env env(*this, envconfig(advisoryDelete));
        auto& store = env.app().getSHAMapStore();

        auto ledgerSeq = waitForReady(env);
        auto lastRotated = ledgerSeq - 1;
        BEAST_EXPECT(store.getLastRotated() == lastRotated);
        BEAST_EXPECT(lastRotated != 2);

        auto canDelete = env.rpc("can_delete");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == 0);

        canDelete = env.rpc("can_delete", "never");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == 0);

        auto const firstBatch = kDeleteInterval + ledgerSeq;
        for (; ledgerSeq < firstBatch; ++ledgerSeq)
        {
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(lastRotated == store.getLastRotated());

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", std::to_string(ledgerSeq + (kDeleteInterval / 2)));
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == ledgerSeq + (kDeleteInterval / 2));

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - 2, 2);
        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off a cleanup, but it stays small.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(syncStore(env));

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - firstBatch, firstBatch);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "always");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(
            canDelete[jss::result][jss::can_delete] == std::numeric_limits<unsigned int>::max());

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(syncStore(env));

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;

        // This does not kick off a cleanup
        canDelete = env.rpc("can_delete", "now");
        BEAST_EXPECT(!rpc::containsError(canDelete[jss::result]));
        BEAST_EXPECT(canDelete[jss::result][jss::can_delete] == ledgerSeq - 1);

        for (; ledgerSeq < lastRotated + kDeleteInterval; ++ledgerSeq)
        {
            // No cleanups in this loop.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq), true));
        }

        BEAST_EXPECT(syncStore(env));

        BEAST_EXPECT(store.getLastRotated() == lastRotated);

        {
            // This kicks off another cleanup.
            env.close();

            auto ledger = env.rpc("ledger", "validated");
            BEAST_EXPECT(goodLedger(env, ledger, std::to_string(ledgerSeq++), true));
        }

        BEAST_EXPECT(syncStore(env));

        ledgerCheck(env, ledgerSeq - lastRotated, lastRotated);

        BEAST_EXPECT(store.getLastRotated() == ledgerSeq - 1);
        lastRotated = ledgerSeq - 1;
    }

    std::unique_ptr<node_store::Backend>
    makeBackendRotating(jtx::Env& env, NodeStoreScheduler& scheduler, std::string path)
    {
        Section section{env.app().config().section(Sections::kNodeDatabase)};
        std::filesystem::path newPath;

        if (!BEAST_EXPECT(path.size()))
            return {};
        newPath = path;
        section.set(Keys::kPath, newPath.string());

        auto backend{node_store::Manager::instance().makeBackend(
            section,
            megabytes(env.app().config().getValueFor(SizedItem::BurstSize, std::nullopt)),
            scheduler,
            env.app().getJournal("NodeStoreTest"))};
        backend->open();
        return backend;
    }

    void
    testRotate()
    {
        // The only purpose of this test is to ensure that if something that
        // should never happen happens, we don't get a deadlock.
        testcase("rotate with lock contention");

        using namespace jtx;
        Env env(*this, envconfig(onlineDelete));

        /////////////////////////////////////////////////////////////
        // Create NodeStore with two backends to allow online deletion of data.
        // Normally, SHAMapStoreImp handles all these details.
        auto nscfg = env.app().config().section(Sections::kNodeDatabase);

        // Provide default values.
        if (!nscfg.exists(Keys::kCacheSize))
        {
            nscfg.set(
                Keys::kCacheSize,
                std::to_string(
                    env.app().config().getValueFor(SizedItem::TreeCacheSize, std::nullopt)));
        }

        if (!nscfg.exists(Keys::kCacheAge))
        {
            nscfg.set(
                Keys::kCacheAge,
                std::to_string(
                    env.app().config().getValueFor(SizedItem::TreeCacheAge, std::nullopt)));
        }

        NodeStoreScheduler scheduler(env.app().getJobQueue());

        std::string const writableDb = "write";
        std::string const archiveDb = "archive";
        auto writableBackend = makeBackendRotating(env, scheduler, writableDb);
        auto archiveBackend = makeBackendRotating(env, scheduler, archiveDb);

        static constexpr int kReadThreads = 4;
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler,
            kReadThreads,
            std::move(writableBackend),
            std::move(archiveBackend),
            nscfg,
            env.app().getJournal("NodeStoreTest"));

        /////////////////////////////////////////////////////////////
        // Check basic functionality
        using namespace std::chrono_literals;
        std::atomic<int> threadNum = 0;

        {
            auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));

            auto const cb = [&](std::string const& writableName, std::string const& archiveName) {
                BEAST_EXPECT(writableName == "1");
                BEAST_EXPECT(archiveName == "write");
                // Ensure that dbr functions can be called from within the
                // callback
                BEAST_EXPECT(dbr->getName() == "1");
            };

            dbr->rotate(std::move(newBackend), cb);
        }
        BEAST_EXPECT(threadNum == 1);
        BEAST_EXPECT(dbr->getName() == "1");

        /////////////////////////////////////////////////////////////
        // Do something stupid. Try to re-enter rotate from inside the callback.
        {
            auto const cb = [&](std::string const& writableName, std::string const& archiveName) {
                BEAST_EXPECT(writableName == "3");
                BEAST_EXPECT(archiveName == "2");
                // Ensure that dbr functions can be called from within the
                // callback
                BEAST_EXPECT(dbr->getName() == "3");
            };
            auto const cbReentrant = [&](std::string const& writableName,
                                         std::string const& archiveName) {
                BEAST_EXPECT(writableName == "2");
                BEAST_EXPECT(archiveName == "1");
                auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
                // Reminder: doing this is stupid and should never happen
                dbr->rotate(std::move(newBackend), cb);
            };
            auto newBackend = makeBackendRotating(env, scheduler, std::to_string(++threadNum));
            dbr->rotate(std::move(newBackend), cbReentrant);
        }

        BEAST_EXPECT(threadNum == 3);
        BEAST_EXPECT(dbr->getName() == "3");
    }

    void
    testLedgerGaps()
    {
        // Note that this test is intentionally very similar to
        // LedgerMaster_test::testCompleteLedgerRange, but has a different
        // focus.

        testcase("Wait for ledger gaps to fill in");

        using namespace test::jtx;

        Env env{*this, envconfig(onlineDelete)};

        auto failureMessage = [&](char const* label, auto expected, auto actual) {
            std::stringstream ss;
            ss << label << ": Expected: " << expected << ", Got: " << actual;
            return ss.str();
        };

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        auto& lm = env.app().getLedgerMaster();
        LedgerIndex minSeq = 2;
        LedgerIndex maxSeq = env.closed()->header().seq;
        auto& store = env.app().getSHAMapStore();
        auto& netOPs = env.app().getOPs();
        BEAST_EXPECT(syncStore(env));
        // The store initializes lastRotated from the first validated ledger it
        // observes, and onLedgerClosed() keeps only the most recent ledger in
        // newLedger_, so validated ledgers arriving while the store thread is
        // busy are coalesced away. Which of the existing complete ledgers wins
        // is therefore a timing detail. Spinning until it equals a hard-coded
        // value never terminates when a different one legitimately wins.
        LedgerIndex lastRotated = store.getLastRotated();
        BEAST_EXPECTS(lastRotated >= minSeq && lastRotated <= maxSeq, std::to_string(lastRotated));
        BEAST_EXPECTS(maxSeq == 3, std::to_string(maxSeq));
        BEAST_EXPECTS(lm.getCompleteLedgers() == "2-3", lm.getCompleteLedgers());
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
        BEAST_EXPECT(minSeq + 1 > maxSeq - 1);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);

        auto expectedRange =
            [](LedgerIndex minSeq, std::vector<LedgerIndex> const& deleteSeqs, LedgerIndex maxSeq) {
                std::stringstream expectedRange;
                expectedRange << minSeq;
                auto lastDelete = minSeq - 1;
                for (auto deleteSeq : deleteSeqs)
                {
                    if (deleteSeq <= lastDelete)
                        continue;
                    expectedRange << "-" << (deleteSeq - 1);
                    if (deleteSeq + 1 <= maxSeq)
                        expectedRange << "," << (deleteSeq + 1);
                    lastDelete = deleteSeq;
                }
                if (lastDelete + 1 < maxSeq)
                {
                    expectedRange << "-" << maxSeq;
                }
                return expectedRange.str();
            };

        auto deleteLedgerSeq =
            [&lm, &store, &netOPs, &minSeq, &lastRotated, &expectedRange, &failureMessage, this](
                Env& env,
                LedgerIndex& maxSeq,
                std::vector<LedgerIndex>& deleteSeqs) -> LedgerIndex {
            using namespace std::chrono_literals;

            // The next ledger will trigger a rotation. Delete the
            // current ledger from LedgerMaster.

            netOPs.setMode(OperatingMode::CONNECTED);

            LedgerIndex const deleteSeq = maxSeq;
            std::size_t iterations = 30;
            while (!lm.haveLedger(deleteSeq) && --iterations > 0)
            {
                std::this_thread::sleep_for(10ms);
            }
            // Even the slowest machines should be able to finalize deleteSeq within 10
            // loops (100ms). If this test ever actually fails feel free to lower this
            // cutoff. The intent of this test is to flag if the loop takes a very long
            // time, but still allow the rest of this function to finish.
            BEAST_EXPECTS(iterations > 20, std::to_string(iterations));
            if (!BEAST_EXPECT(lm.haveLedger(deleteSeq)))
                return 0;

            // This test may be timing sensitive, because it's messing with server internals in ways
            // that they can't be messed with normally. Sleep a little bit to give the server time
            // to finish any internal work before we delete the ledger.
            std::this_thread::sleep_for(250ms);

            lm.clearLedger(deleteSeq);
            deleteSeqs.push_back(deleteSeq);
            if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                return 0;

            BEAST_EXPECTS(
                lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeqs, maxSeq),
                failureMessage(
                    "Complete ledgers",
                    expectedRange(minSeq, deleteSeqs, maxSeq),
                    lm.getCompleteLedgers()));
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == deleteSeqs.size());

            if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                return 0;
            // Close another ledger, which will trigger a rotation, but the
            // rotation will be stuck until the missing ledger is filled in.
            env.close();
            // Do not call rendezvous() here without a timeout; it will block until the missing
            // ledger is backfilled. That will not happen automatically. It's a manual step that
            // is done later in this test.
            ++maxSeq;

            if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                return 0;
            netOPs.setMode(OperatingMode::FULL);

            if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                return 0;
            BEAST_EXPECT(!store.rendezvous(10ms));
            BEAST_EXPECT(netOPs.getOperatingMode() == OperatingMode::FULL);

            // Nothing has changed
            BEAST_EXPECTS(
                store.getLastRotated() == lastRotated,
                failureMessage("lastRotated", lastRotated, store.getLastRotated()));
            BEAST_EXPECTS(
                lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeqs, maxSeq),
                failureMessage(
                    "Complete ledgers",
                    expectedRange(minSeq, deleteSeqs, maxSeq),
                    lm.getCompleteLedgers()));

            return deleteSeq;
        };

        std::vector<LedgerIndex> deleteSeqs;

        // Close enough ledgers to rotate a few times
        while (maxSeq < 40)
        {
            for (int t = 0; t < 3; ++t)
            {
                env(noop(alice));
            }
            env.close();
            BEAST_EXPECT(syncStore(env));

            ++maxSeq;

            if (maxSeq + 1 == lastRotated + kDeleteInterval)
            {
                using namespace std::chrono_literals;

                {
                    // Trigger the circuit breaker in SHAMapStoreImp::healthWait() to ensure it
                    // doesn't block forever.
                    LedgerIndex const deleteSeq = deleteLedgerSeq(env, maxSeq, deleteSeqs);
                    if (!BEAST_EXPECT(deleteSeq > 0))
                        return;
                    if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                        return;

                    // Close 7 more ledgers, waiting a little bit in between to
                    // simulate the ledger making progress while online delete waits
                    // for the missing ledger to be filled in.
                    // After the 7th ledger, the circuit breaker will trigger and abort the attempt.
                    while (maxSeq < lastRotated + (kDeleteInterval * 2) - 2)
                    {
                        env.close();
                        ++maxSeq;
                        // Nothing has changed
                        BEAST_EXPECTS(
                            store.getLastRotated() == lastRotated,
                            failureMessage("lastRotated", lastRotated, store.getLastRotated()));
                        BEAST_EXPECTS(
                            lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeqs, maxSeq),
                            failureMessage(
                                "Complete Ledgers",
                                expectedRange(minSeq, deleteSeqs, maxSeq),
                                lm.getCompleteLedgers()));
                        // The Store is "stuck" in healthWait() and won't finish the run() loop
                        // until it's backfilled
                        if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                            return;
                    }

                    // Close one more ledger, which will NOT trigger the circuit breaker. Wait for
                    // the full 1 second recovery wait timeout to ensure the circuit breaker is not
                    // triggered.
                    env.close();
                    ++maxSeq;
                    // The Store is "stuck" in healthWait() and won't finish the run() loop
                    // until it's backfilled
                    BEAST_EXPECT(!store.rendezvous(1s));

                    // Close one more ledger, which will trigger the circuit breaker and abort the
                    // attempt to rotate.
                    env.close();
                    ++maxSeq;
                    // Nothing has changed
                    BEAST_EXPECTS(
                        store.getLastRotated() == lastRotated,
                        failureMessage("lastRotated", lastRotated, store.getLastRotated()));
                    BEAST_EXPECTS(
                        lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeqs, maxSeq),
                        failureMessage(
                            "Complete Ledgers",
                            expectedRange(minSeq, deleteSeqs, maxSeq),
                            lm.getCompleteLedgers()));

                    // The circuit breaker has been triggered.
                    BEAST_EXPECT(syncStore(env));
                }
                {
                    // Recover before the circuit breaker triggers, so the test can continue.
                    LedgerIndex const deleteSeq = deleteLedgerSeq(env, maxSeq, deleteSeqs);
                    if (!BEAST_EXPECT(deleteSeq > 0))
                        return;
                    if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                        return;

                    // Close 5 more ledgers, waiting a little bit in between to
                    // simulate the ledger making progress while online delete waits
                    // for the missing ledger to be filled in.
                    // This ensures the healthWait check has time to run and
                    // detect the gap.
                    for (int l = 0; l < 5; ++l)
                    {
                        env.close();
                        ++maxSeq;
                        // Nothing has changed
                        BEAST_EXPECTS(
                            store.getLastRotated() == lastRotated,
                            failureMessage("lastRotated", lastRotated, store.getLastRotated()));
                        BEAST_EXPECTS(
                            lm.getCompleteLedgers() == expectedRange(minSeq, deleteSeqs, maxSeq),
                            failureMessage(
                                "Complete Ledgers",
                                expectedRange(minSeq, deleteSeqs, maxSeq),
                                lm.getCompleteLedgers()));
                        if (!BEAST_EXPECT(!lm.haveLedger(deleteSeq)))
                            return;
                    }

                    // The Store is "stuck" in healthWait() and won't finish the run() loop
                    // until it's backfilled
                    // Wait for the full 1 second recovery wait timeout to ensure the circuit
                    // breaker is not triggered, and this isn't some other timing fluke.
                    BEAST_EXPECT(!store.rendezvous(1s));

                    // Put the missing ledger back in LedgerMaster
                    lm.setLedgerRangePresent(deleteSeq, deleteSeq);
                    BEAST_EXPECT(deleteSeqs.back() == deleteSeq);
                    deleteSeqs.pop_back();

                    // Wait for the rotation to finish
                    BEAST_EXPECT(syncStore(env));

                    minSeq = lastRotated;
                    while (deleteSeqs.front() < minSeq)
                    {
                        deleteSeqs.erase(deleteSeqs.begin());
                    }
                    lastRotated = deleteSeq + 1;
                }
            }
            BEAST_EXPECT(maxSeq != lastRotated + kDeleteInterval);
            BEAST_EXPECTS(
                env.closed()->header().seq == maxSeq,
                failureMessage("maxSeq", maxSeq, env.closed()->header().seq));
            BEAST_EXPECTS(
                store.getLastRotated() == lastRotated,
                failureMessage("lastRotated", lastRotated, store.getLastRotated()));
            {
                auto const expected = expectedRange(minSeq, deleteSeqs, maxSeq);
                BEAST_EXPECTS(
                    lm.getCompleteLedgers() == expected,
                    failureMessage("CompleteLedgers", expected, lm.getCompleteLedgers()));
            }
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == deleteSeqs.size());
            // missingFromCompleteLedgerRange() treats first > last as a
            // precondition violation and aborts a Debug build via UNREACHABLE.
            // The range can only collapse if this test's model of minSeq /
            // maxSeq has desynced from the store, so report that as a failure
            // instead of taking down the whole unit test job.
            if (minSeq + 1 <= maxSeq - 1)
            {
                BEAST_EXPECT(
                    lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == deleteSeqs.size());
            }
            else
            {
                BEAST_EXPECTS(false, failureMessage("range collapsed", minSeq, maxSeq));
            }
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == deleteSeqs.size() + 2);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == deleteSeqs.size() + 2);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == deleteSeqs.size() + 2);
        }
    }

    // Cover the branches of SHAMapStoreImp::healthWait() that decide whether
    // the server is healthy enough to rotate, and how loudly to complain while
    // it is not. testLedgerGaps() covers the case where a gap holds the
    // rotation back until the circuit breaker trips; these cover the rest of
    // the decision table.
    void
    testHealthWaitState()
    {
        testcase("healthWait server state");

        using namespace std::chrono_literals;
        using namespace test::jtx;

        auto logs = std::make_unique<StoreLogs>();
        auto const* const log = logs.get();
        Env env{*this, envconfig(onlineDelete), std::move(logs), beast::Severity::Trace};

        auto& store = env.app().getSHAMapStore();
        auto& netOPs = env.app().getOPs();

        // No gap: the only thing holding the store back is the operating mode.
        auto const parked = parkInHealthWait(env, false, OperatingMode::CONNECTED);
        if (!parked)
            return;

        // Hold the non-FULL mode for longer than recovery_wait_seconds so the
        // store is certain to sample it and log at least once. With no gap, a
        // fresh validated ledger and a mode that is not DISCONNECTED, the only
        // check left that can report unhealthy is "mode != FULL".
        BEAST_EXPECT(!store.rendezvous(1500ms));
        BEAST_EXPECT(netOPs.getOperatingMode() != OperatingMode::FULL);
        BEAST_EXPECT(netOPs.getOperatingMode() != OperatingMode::DISCONNECTED);
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));
        // A mode that is not FULL is not expected to fix itself, so the wait is
        // logged at warn, for the full duration.
        BEAST_EXPECT(log->count(beast::Severity::Warning, kFullWait) > 0);

        // Now make the mode FULL but the validated ledger stale. Advancing the
        // clock without closing a ledger ages the validated ledger past
        // age_threshold_seconds, which defaults to 60. The mode check can no
        // longer be the reason the store is unhealthy, so the age check is.
        auto const closeTime = env.now();
        env.timeKeeper().set(closeTime + 2min);
        netOPs.setMode(OperatingMode::FULL);
        BEAST_EXPECT(netOPs.getOperatingMode() == OperatingMode::FULL);
        BEAST_EXPECT(!store.rendezvous(1500ms));
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));

        // Restore the clock. Nothing is wrong any more, so the rotation that
        // has been waiting all along runs to completion.
        env.timeKeeper().set(closeTime);
        BEAST_EXPECT(syncStore(env));
        BEAST_EXPECTS(
            store.getLastRotated() == parked->validated, std::to_string(store.getLastRotated()));
    }

    void
    testHealthWaitGapLevels()
    {
        testcase("healthWait gap wait levels");

        using namespace std::chrono_literals;
        using namespace test::jtx;

        auto logs = std::make_unique<StoreLogs>();
        auto const* const log = logs.get();
        Env env{*this, envconfig(onlineDelete), std::move(logs), beast::Severity::Trace};

        auto& lm = env.app().getLedgerMaster();
        auto& store = env.app().getSHAMapStore();

        auto const parked = parkInHealthWait(env, true, OperatingMode::FULL);
        if (!parked)
            return;

        // The missing ledger is an older one; the validated ledger itself is
        // present. The store has no reason to think the gap will close on its
        // own, so it waits the full duration and says so at info -- not warn,
        // because the server is otherwise healthy and has not been waiting long
        // enough to have fallen behind.
        BEAST_EXPECT(lm.haveLedger(parked->validated));
        BEAST_EXPECT(!lm.haveLedger(parked->gap));
        BEAST_EXPECT(!store.rendezvous(1500ms));
        BEAST_EXPECT(log->count(beast::Severity::Info, kFullWait) > 0);
        // Nothing so far should have looked like a ledger being built.
        BEAST_EXPECT(log->count(beast::Severity::Trace, kShortWait) == 0);

        // Move the gap onto the validated ledger itself. That is the one case
        // the store treats as transient -- the ledger is expected to be built
        // shortly -- so it drops to trace and waits a tenth as long. Asserting
        // that the shortened wait appears only after this swap is what pins the
        // branch to the buildingIndex condition, rather than to anything
        // incidental about a store that happens to be waiting.
        lm.setLedgerRangePresent(parked->gap, parked->gap);
        lm.clearLedger(parked->validated);
        BEAST_EXPECT(lm.haveLedger(parked->gap));
        BEAST_EXPECT(!lm.haveLedger(parked->validated));
        BEAST_EXPECT(!store.rendezvous(1500ms));
        BEAST_EXPECT(log->count(beast::Severity::Trace, kShortWait) > 0);
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));

        // Fill it in and the rotation completes.
        lm.setLedgerRangePresent(parked->validated, parked->validated);
        BEAST_EXPECT(syncStore(env));
        BEAST_EXPECTS(
            store.getLastRotated() == parked->validated, std::to_string(store.getLastRotated()));
    }

    void
    testHealthWaitDisconnected()
    {
        testcase("healthWait disconnected");

        using namespace std::chrono_literals;
        using namespace test::jtx;

        Env env{*this, envconfig(onlineDelete)};

        auto& lm = env.app().getLedgerMaster();
        auto& store = env.app().getSHAMapStore();
        auto& netOPs = env.app().getOPs();

        auto const parked = parkInHealthWait(env, true, OperatingMode::FULL);
        if (!parked)
            return;

        // While the server is FULL, the gap holds the rotation back.
        BEAST_EXPECT(!store.rendezvous(1500ms));
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));

        // A disconnected server is not doing any ledger I/O, so the gap cannot
        // have been caused by its own activity and will not close until it has
        // peers again. The store deliberately takes advantage of that to get as
        // much rotation done as possible: this is the one case where a gap does
        // not hold online delete back at all.
        netOPs.setMode(OperatingMode::DISCONNECTED);
        BEAST_EXPECT(netOPs.getOperatingMode() == OperatingMode::DISCONNECTED);
        BEAST_EXPECT(syncStore(env));
        BEAST_EXPECTS(
            store.getLastRotated() == parked->validated, std::to_string(store.getLastRotated()));
        // The rotation ran with the gap still present -- nothing filled it in.
        BEAST_EXPECT(!lm.haveLedger(parked->gap));
    }

    void
    testHealthWaitStop()
    {
        testcase("healthWait stop");

        using namespace test::jtx;

        Env env{*this, envconfig(onlineDelete)};

        auto& store = env.app().getSHAMapStore();

        auto const parked = parkInHealthWait(env, true, OperatingMode::FULL);
        if (!parked)
            return;

        // Stopping the store has to break it out of the wait loop, which it
        // would otherwise never leave: the gap is never filled in and the
        // validated ledger index never advances to reach the circuit breaker.
        //
        // stop() joins the store's thread, so its return is the
        // synchronisation point here. Deliberately do not call the untimed
        // rendezvous() afterwards: run() returns from the Stopping arm without
        // clearing working_, so rendezvous() would block forever.
        store.stop();
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));
    }

    // The two tests below cover the health check that run() performs between the
    // stages of a rotation it has already committed to, which is a different
    // decision from the one that gates the rotation in the first place: giving
    // up here means abandoning work in progress. run() makes it at four points
    // -- after clearing prior ledgers, after copying the validated ledger, after
    // freshening the caches, and after clearing them -- with the same three-way
    // switch each time, and parkMidRotation() parks the store at the first of
    // them.
    void
    testHealthWaitExpiredMidRotation()
    {
        testcase("healthWait circuit breaker mid-rotation");

        using namespace test::jtx;

        auto logs = std::make_unique<StoreLogs>();
        auto* const log = logs.get();
        Env env{*this, envconfig(slowOnlineDelete), std::move(logs), beast::Severity::Trace};

        auto& lm = env.app().getLedgerMaster();
        auto& store = env.app().getSHAMapStore();

        auto const parked = parkMidRotation(env, *log);
        if (!parked)
            return;

        // Advance the validated ledger index past the circuit breaker. The store
        // has had no successful health check since the gap appeared, so once the
        // index has moved max_waiting_ledgers on from the last one that did
        // succeed, it abandons the rotation instead of waiting for the gap
        // forever. Nothing here fills the gap in.
        for (int i = 0; i < kMinWaitingLedgers; ++i)
        {
            env.close();
            BEAST_EXPECT(!lm.haveLedger(parked->gap));
        }

        // Abandoning the rotation returns the store to waiting for work, so it
        // reports itself idle -- but with lastRotated left where it started,
        // unlike the completed rotation parkMidRotation() drove first.
        BEAST_EXPECT(syncStore(env));
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));
        BEAST_EXPECT(log->count(kExpired) > 0);
        BEAST_EXPECTS(log->count(kFinished) == 1, std::to_string(log->count(kFinished)));
        BEAST_EXPECT(!lm.haveLedger(parked->gap));
    }

    void
    testHealthWaitStopMidRotation()
    {
        testcase("healthWait stop mid-rotation");

        using namespace test::jtx;

        auto logs = std::make_unique<StoreLogs>();
        auto* const log = logs.get();
        Env env{*this, envconfig(slowOnlineDelete), std::move(logs), beast::Severity::Trace};

        auto& store = env.app().getSHAMapStore();

        auto const parked = parkMidRotation(env, *log);
        if (!parked)
            return;

        // Stopping has to break the store out of the rotation, which it would
        // otherwise never leave: the gap is never filled in and the validated
        // ledger index never advances to reach the circuit breaker. Note that
        // being stopped outranks being healthy -- the health check reports it
        // even when nothing is wrong with the server -- so this does not depend
        // on the store still being parked when stop() lands.
        //
        // stop() joins the store's thread, so its return is the synchronisation
        // point. Deliberately do not call the untimed rendezvous() afterwards:
        // run() returns without clearing working_, so it would block forever.
        store.stop();
        BEAST_EXPECTS(
            store.getLastRotated() == parked->lastRotated, std::to_string(store.getLastRotated()));
        // The rotation was abandoned rather than completed, and the circuit
        // breaker was not what abandoned it.
        BEAST_EXPECTS(log->count(kFinished) == 1, std::to_string(log->count(kFinished)));
        BEAST_EXPECTS(log->count(kExpired) == 0, std::to_string(log->count(kExpired)));
    }

    void
    run() override
    {
        testConfig();
        testClear();
        testAutomatic();
        testCanDelete();
        testRotate();
        testLedgerGaps();
        testHealthWaitState();
        testHealthWaitGapLevels();
        testHealthWaitDisconnected();
        testHealthWaitStop();
        testHealthWaitExpiredMidRotation();
        testHealthWaitStopMidRotation();
    }
};

// VFALCO This test fails because of thread asynchronous issues
BEAST_DEFINE_TESTSUITE(SHAMapStore, app, xrpl);

}  // namespace xrpl::test
