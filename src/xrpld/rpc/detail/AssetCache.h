#pragma once

#include <xrpld/rpc/detail/MPT.h>
#include <xrpld/rpc/detail/TrustLine.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace xrpl {

/**
 * Shared cache of pathfinding assets (trust lines / MPTs).
 *
 * Design (tuned for continuous WS path_find under load):
 * - One outgoing line vector per account, filled in chunks (kPathFindLineChunkSize)
 *   with a resumable owner-dir cursor so large accounts load across updates.
 * - Callers apply currency / Incoming filters inline (zero per-hop alloc).
 * - Vectors are reused across a few ledger advances without fingerprinting.
 * - Global line budget bounds worst-case memory (hard stop when remaining == 0;
 *   no silent floor). Incomplete accounts grow on expandIncompleteLines.
 * - Hits use shared_lock; misses/expands load under unique lock (single-flight).
 * - Soft ledger advance keeps complete line vectors; incomplete progressive
 *   fills drop and reload (DirCursor is not stable across ledger page changes).
 * - Per-session account pins: an entry is freed only when every path_find that
 *   used it has ended. Shared hubs stay warm for remaining sessions (no LRU
 *   thrash during ramp-down). When the last subscription ends the whole cache
 *   is dropped by PathRequestManager.
 */
class AssetCache final : public CountedObject<AssetCache>
{
public:
    explicit AssetCache(
        std::shared_ptr<ReadView const> l,
        beast::Journal j,
        std::size_t maxTotalLines = rpc::tuning::kPathFindMaxTotalLines,
        std::size_t maxLinesPerAccount = rpc::tuning::kPathFindMaxLinesPerAccount,
        std::uint32_t cacheReuseLedgers = rpc::tuning::kPathCacheReuseLedgers,
        std::size_t lineChunkSize = rpc::tuning::kPathFindLineChunkSize);
    ~AssetCache();

    /**
     * RAII: pin account loads to a path_find session id for this thread.
     * getRippleLines records pins so releaseSession can drop unreferenced
     * accounts when that session ends — without evicting hubs still held by
     * other live sessions.
     */
    class SessionPin
    {
    public:
        SessionPin(int sessionId) noexcept;
        ~SessionPin() noexcept;
        SessionPin(SessionPin const&) = delete;
        SessionPin&
        operator=(SessionPin const&) = delete;

    private:
        int prev_{0};
    };

    /**
     * RAII: per-thread line load/expand budget (owner-dir chunk size).
     * - WebSocket path_find: leave default (kPathFindLineChunkSize) so lines
     *   fill slowly across updates.
     * - One-shot (ripple_path_find, transactionSign build_path): set to
     *   maxLinesPerAccount so first load / expandIncompleteLines pull as many
     *   lines as budget allows in a single request (not just 64).
     */
    class LoadScope
    {
    public:
        explicit LoadScope(std::size_t chunkLines) noexcept;
        ~LoadScope() noexcept;
        LoadScope(LoadScope const&) = delete;
        LoadScope&
        operator=(LoadScope const&) = delete;

    private:
        std::size_t prev_{0};
    };

    /**
     * Snapshot of the current ledger view. Returns a shared_ptr by value under
     * lock so callers keep a stable ReadView even if advanceLedger runs.
     */
    [[nodiscard]] std::shared_ptr<ReadView const>
    getLedger() const;

    /**
     * Point the cache at a newer ledger.
     * forceClear drops all entries and session pins; otherwise vectors are
     * retained and only reloaded on access once older than cacheReuseLedgers_.
     */
    void
    advanceLedger(std::shared_ptr<ReadView const> const& ledger, bool forceClear = false);

    /**
     * Full outgoing trust-line vector for an account (shared ownership).
     * When a SessionPin is active on this thread, the account is pinned to
     * that session until releaseSession(sessionId).
     *
     * First miss loads at most the thread LoadScope chunk (default
     * lineChunkSize_). Call expandIncompleteLines to append more.
     */
    std::shared_ptr<std::vector<PathFindTrustLine>>
    getRippleLines(AccountID const& accountID);

    /**
     * Append one chunk of trust lines to each incomplete cached account,
     * while global/per-account budget allows. Copy-on-write so existing
     * shared vectors stay stable for concurrent readers.
     *
     * @return true if any account's line vector grew.
     */
    bool
    expandIncompleteLines();

    /**
     * Like expandIncompleteLines, but only accounts pinned by sessionId.
     * Used by one-shot ripple_path_find when it shares AssetCache with WS
     * sessions so a legacy drain cannot expand every hub on the node.
     *
     * @return true if any pinned account's line vector grew.
     */
    bool
    expandIncompleteLinesForSession(int sessionId);

    /**
     * True if any cached account still has a residual owner-dir cursor
     * (partial line set).
     */
    [[nodiscard]] bool
    hasIncompleteLines() const;

    /**
     * True if any account pinned by sessionId still has an incomplete
     * owner-dir cursor. Used for path_find `warning: path_lines_partial`
     * so unrelated sessions are not flagged for a shared-cache whale.
     */
    [[nodiscard]] bool
    hasIncompleteLinesForSession(int sessionId) const;

    /**
     * Bumped whenever any account's line vector grows (first load or expand).
     * PathRequest compares against a per-session snapshot to decide whether
     * progressive fills warrant a Pathfinder pass.
     */
    [[nodiscard]] std::uint64_t
    lineEpoch() const
    {
        return lineEpoch_.load(std::memory_order_relaxed);
    }

    /**
     * MPTs for an account. Returns shared_ptr by value (never a map reference).
     */
    std::shared_ptr<std::vector<PathFindMPT>>
    getMPTs(AccountID const& account);

    /**
     * Drop all account pins held by sessionId. Any line vector whose pin count
     * reaches zero is erased (PathFindTrustLine memory reclaimed). Safe and
     * idempotent if called more than once for the same session.
     *
     * @return Number of PathFindTrustLine objects freed.
     */
    std::size_t
    releaseSession(int sessionId);

    [[nodiscard]] std::size_t
    totalLineCount() const
    {
        return totalLineCount_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool
    overBudget() const
    {
        return totalLineCount_.load(std::memory_order_relaxed) >= maxTotalLines_;
    }

    [[nodiscard]] std::uint64_t
    cacheHits() const
    {
        return cacheHits_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t
    cacheMisses() const
    {
        return cacheMisses_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t
    linesLoaded() const
    {
        return linesLoaded_.load(std::memory_order_relaxed);
    }
    /**
     * Number of times advanceLedger advanced the cache view (soft or force).
     * Not only full rebuilds — name reflects ledger advances.
     */
    [[nodiscard]] std::uint64_t
    ledgerAdvances() const
    {
        return ledgerAdvances_.load(std::memory_order_relaxed);
    }

private:
    struct LineEntry
    {
        /**
         * Published line vector (may be held by concurrent Pathfinder readers).
         */
        std::shared_ptr<std::vector<PathFindTrustLine>> lines;
        /**
         * Chunks appended while `lines` was shared (use_count > 1). Expand never
         * copies the published vector; readers see pending only after coalesce.
         */
        std::vector<std::shared_ptr<std::vector<PathFindTrustLine>>> pending;
        std::uint32_t loadedSeq{0};
        /**
         * Number of path_find sessions currently pinning this account.
         */
        std::size_t pinCount{0};
        /**
         * Resume point for progressive owner-dir fill. complete == true means
         * the directory was fully scanned (or hit per-account cap).
         */
        PathFindTrustLine::DirCursor cursor{};

        [[nodiscard]] std::size_t
        storedLineCount() const
        {
            std::size_t n = lines ? lines->size() : 0;
            for (auto const& p : pending)
            {
                if (p)
                    n += p->size();
            }
            return n;
        }
    };

    std::shared_ptr<std::vector<PathFindTrustLine>>
    getOrLoadOutgoing(AccountID const& accountID);

    /**
     * Caller must hold lock_ exclusively.
     */
    std::shared_ptr<std::vector<PathFindTrustLine>>
    loadOutgoingUnlocked(AccountID const& accountID);

    /**
     * Caller must hold lock_ exclusively. Append at most one chunk.
     * @return number of lines added.
     */
    std::size_t
    expandAccountUnlocked(AccountID const& accountID, LineEntry& entry);

    /**
     * Fold pending chunks into lines for publication. Prefer in-place absorb
     * when sole owner; only then may allocate a full replacement vector.
     * Caller must hold lock_ exclusively.
     */
    void
    coalescePendingUnlocked(LineEntry& entry);

    /**
     * Caller must hold lock_ exclusively. First pin per session increments.
     */
    void
    pinAccountUnlocked(int sessionId, AccountID const& accountID);

    [[nodiscard]] std::size_t
    remainingBudgetUnlocked() const;

    /**
     * LoadScope override if set, otherwise configured lineChunkSize_.
     */
    [[nodiscard]] std::size_t
    effectiveChunkSize() const;

    mutable std::shared_mutex lock_;

    std::shared_ptr<ReadView const> ledger_;
    beast::Journal journal_;
    std::size_t maxTotalLines_;
    std::size_t maxLinesPerAccount_;
    std::uint32_t cacheReuseLedgers_;
    std::size_t lineChunkSize_;

    hash_map<AccountID, LineEntry> lines_;
    std::atomic<std::size_t> totalLineCount_{0};
    hash_map<AccountID, std::shared_ptr<std::vector<PathFindMPT>>> mpts_;

    /**
     * sessionId → accounts that session has pinned (for O(session) release).
     */
    hash_map<int, hash_set<AccountID>> sessionAccounts_;

    std::atomic<std::uint64_t> cacheHits_{0};
    std::atomic<std::uint64_t> cacheMisses_{0};
    std::atomic<std::uint64_t> linesLoaded_{0};
    std::atomic<std::uint64_t> ledgerAdvances_{0};
    std::atomic<std::uint64_t> lineEpoch_{0};
};

}  // namespace xrpl
