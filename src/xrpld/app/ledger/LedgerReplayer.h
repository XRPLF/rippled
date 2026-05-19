#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerReplayTask.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/beast/utility/Journal.h>

#include <mutex>
#include <vector>

namespace xrpl {

namespace test {
class LedgerReplayClient;
}  // namespace test

namespace LedgerReplayParameters {
// timeout value for LedgerReplayTask
constexpr auto kTaskTimeout = std::chrono::milliseconds{500};

// for LedgerReplayTask to calculate max allowed timeouts
// = max( kTaskMaxTimeoutsMinimum,
//        (# of ledger to replay) * kTaskMaxTimeoutsMultiplier)
constexpr std::uint32_t kTaskMaxTimeoutsMultiplier = 2;
constexpr std::uint32_t kTaskMaxTimeoutsMinimum = 10;

// timeout value for subtasks: LedgerDeltaAcquire and SkipListAcquire
constexpr auto kSubTaskTimeout = std::chrono::milliseconds{250};
// max of allowed subtask timeouts
constexpr std::uint32_t kSubTaskMaxTimeouts = 10;

// max number of peers that do not support the ledger replay feature
// returned by the PeerSet before switch to fallback
constexpr auto kMaxNoFeaturePeerCount = 2;
// subtask timeout value after fallback
constexpr auto kSubTaskFallbackTimeout = std::chrono::milliseconds{1000};

// for LedgerReplayer to limit the number of LedgerReplayTask
constexpr std::uint32_t kMaxTasks = 10;

// for LedgerReplayer to limit the number of ledgers to replay in one task
constexpr std::uint32_t kMaxTaskSize = 256;

// to limit the number of LedgerReplay related jobs in JobQueue
constexpr std::uint32_t kMaxQueuedTasks = 100;
}  // namespace LedgerReplayParameters

/**
 * Manages the lifetime of ledger replay tasks.
 */
class LedgerReplayer final
{
public:
    LedgerReplayer(
        Application& app,
        InboundLedgers& inboundLedgers,
        std::unique_ptr<PeerSetBuilder> peerSetBuilder);

    ~LedgerReplayer();

    /**
     * Replay a range of ledgers
     * @param r  reason for the replay request
     * @param finishLedgerHash  hash of the last ledger
     * @param totalNumLedgers  total number of ledgers in the range, inclusive
     * @note totalNumLedgers must > 0 && totalNumLedgers must <= 256
     */
    void
    replay(InboundLedger::Reason r, uint256 const& finishLedgerHash, std::uint32_t totalNumLedgers);

    /** Create LedgerDeltaAcquire subtasks for the LedgerReplayTask task */
    void
    createDeltas(std::shared_ptr<LedgerReplayTask> task);

    /**
     * Process a skip list (extracted from a TMProofPathResponse message)
     * @param info  ledger info
     * @param data  skip list holder
     * @note  info and data must have been verified against the ledger hash
     */
    void
    gotSkipList(LedgerHeader const& info, boost::intrusive_ptr<SHAMapItem const> const& data);

    /**
     * Process a ledger delta (extracted from a TMReplayDeltaResponse message)
     * @param info  ledger info
     * @param txns  set of Txns of the ledger
     * @note info and txns must have been verified against the ledger hash
     */
    void
    gotReplayDelta(
        LedgerHeader const& info,
        std::map<std::uint32_t, std::shared_ptr<STTx const>>&& txns);

    /** Remove completed tasks */
    void
    sweep();

    void
    stop();

    std::size_t
    tasksSize() const
    {
        std::scoped_lock const lock(mtx_);
        return tasks_.size();
    }

    std::size_t
    deltasSize() const
    {
        std::scoped_lock const lock(mtx_);
        return deltas_.size();
    }

    std::size_t
    skipListsSize() const
    {
        std::scoped_lock const lock(mtx_);
        return skipLists_.size();
    }

private:
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<LedgerReplayTask>> tasks_;
    hash_map<uint256, std::weak_ptr<LedgerDeltaAcquire>> deltas_;
    hash_map<uint256, std::weak_ptr<SkipListAcquire>> skipLists_;

    Application& app_;
    InboundLedgers& inboundLedgers_;
    std::unique_ptr<PeerSetBuilder> peerSetBuilder_;
    beast::Journal j_;

    friend class test::LedgerReplayClient;
};

}  // namespace xrpl
