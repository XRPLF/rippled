//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_SHARD_H_INCLUDED
#define RIPPLE_NODESTORE_SHARD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/nodestore/Scheduler.h>
#include <ripple/nodestore/impl/DeterministicShard.h>

#include <boost/filesystem.hpp>
#include <nudb/nudb.hpp>

#include <atomic>

namespace ripple {
namespace NodeStore {

using PCache = TaggedCache<uint256, NodeObject>;
using NCache = KeyCache<uint256>;
class DatabaseShard;

/* A range of historical ledgers backed by a node store.
   Shards are indexed and store `ledgersPerShard`.
   Shard `i` stores ledgers starting with sequence: `1 + (i * ledgersPerShard)`
   and ending with sequence: `(i + 1) * ledgersPerShard`.
   Once a shard has all its ledgers, it is never written to again.

   Public functions can be called concurrently from any thread.
*/
class Shard final
{
public:
    enum class State {
        acquire,     // Being acquired
        complete,    // Backend contains all ledgers but is not yet final
        finalizing,  // Being finalized
        final        // Database verified, shard is immutable
    };

    static constexpr State acquire = State::acquire;
    static constexpr State complete = State::complete;
    static constexpr State finalizing = State::finalizing;
    static constexpr State final = State::final;

    Shard(
        Application& app,
        DatabaseShard const& db,
        std::uint32_t index,
        boost::filesystem::path const& dir,
        beast::Journal j);

    Shard(
        Application& app,
        DatabaseShard const& db,
        std::uint32_t index,
        beast::Journal j);

    ~Shard();

    /** Initialize shard.

        @param scheduler The scheduler to use for performing asynchronous tasks.
        @param context The context to use for the backend.
    */
    [[nodiscard]] bool
    init(Scheduler& scheduler, nudb::context& context);

    /** Returns true if the database are open.
     */
    [[nodiscard]] bool
    isOpen() const;

    /** Try to close databases if not in use.

        @return true if databases were closed.
     */
    bool
    tryClose();

    /** Notify shard to prepare for shutdown.
     */
    void
    stop()
    {
        stop_ = true;
    }

    [[nodiscard]] std::optional<std::uint32_t>
    prepare();

    [[nodiscard]] bool
    storeNodeObject(std::shared_ptr<NodeObject> const& nodeObject);

    [[nodiscard]] std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, FetchReport& fetchReport);

    /** Store a ledger.

        @param srcLedger The ledger to store.
        @param next The ledger that immediately follows srcLedger, can be null.
        @return StoreLedgerResult containing data about the store.
    */
    struct StoreLedgerResult
    {
        std::uint64_t count{0};  // Number of storage calls
        std::uint64_t size{0};   // Number of bytes stored
        bool error{false};
    };

    [[nodiscard]] StoreLedgerResult
    storeLedger(
        std::shared_ptr<Ledger const> const& srcLedger,
        std::shared_ptr<Ledger const> const& next);

    [[nodiscard]] bool
    setLedgerStored(std::shared_ptr<Ledger const> const& ledger);

    [[nodiscard]] bool
    containsLedger(std::uint32_t ledgerSeq) const;

    void
    sweep();

    [[nodiscard]] std::uint32_t
    index() const
    {
        return index_;
    }

    [[nodiscard]] boost::filesystem::path const&
    getDir() const
    {
        return dir_;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point
    getLastUse() const;

    /** Returns a pair where the first item describes the storage space
        utilized and the second item is the number of file descriptors required.
    */
    [[nodiscard]] std::pair<std::uint64_t, std::uint32_t>
    getFileInfo() const;

    [[nodiscard]] State
    getState() const
    {
        return state_;
    }

    [[nodiscard]] std::int32_t
    getWriteLoad();

    /** Returns `true` if shard is older, without final key data
     */
    [[nodiscard]] bool
    isLegacy() const;

    /** Finalize shard by walking its ledgers, verifying each Merkle tree and
       creating a deterministic backend.

        @param writeSQLite If true, SQLite entries will be rewritten using
        verified backend data.
        @param referenceHash If present, this hash must match the hash
        of the last ledger in the shard.
    */
    [[nodiscard]] bool
    finalize(bool writeSQLite, std::optional<uint256> const& referenceHash);

    /** Enables removal of the shard directory on destruction.
     */
    void
    removeOnDestroy()
    {
        removeOnDestroy_ = true;
    }

    /**
     * @brief callForLedgerSQL Checks out ledger database for the shard and
     *        calls given callback function passing shard index and session
     *        with the database to it.
     * @param callback Callback function to call.
     * @return Value returned by callback function.
     */
    bool
    callForLedgerSQL(
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback);

    /**
     * @brief callForTransactionSQL Checks out transaction database for the
     *        shard and calls given callback function passing shard index and
     *        session with the database to it.
     * @param callback Callback function to call.
     * @return Value returned by callback function.
     */
    bool
    callForTransactionSQL(
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback);

    // Current shard version
    static constexpr std::uint32_t version{2};

    // The finalKey is a hard coded value of zero. It is used to store
    // finalizing shard data to the backend. The data contains a version,
    // last ledger's hash, and the first and last ledger sequences.
    static uint256 const finalKey;

private:
    class Count final
    {
    public:
        Count(Count const&) = delete;
        Count&
        operator=(Count&&) = delete;
        Count&
        operator=(Count const&) = delete;

        Count(Count&& other) : counter_(other.counter_)
        {
            other.counter_ = nullptr;
        }

        Count(std::atomic<std::uint32_t>* counter) : counter_(counter)
        {
            if (counter_)
                ++(*counter_);
        }

        ~Count()
        {
            if (counter_)
                --(*counter_);
        }

        operator bool() const
        {
            return counter_ != nullptr;
        }

    private:
        std::atomic<std::uint32_t>* counter_;
    };

    struct AcquireInfo
    {
        // SQLite database to track information about what has been acquired
        std::unique_ptr<DatabaseCon> SQLiteDB;

        // Tracks the sequences of ledgers acquired and stored in the backend
        RangeSet<std::uint32_t> storedSeqs;
    };

    Application& app_;
    beast::Journal const j_;
    mutable std::mutex mutex_;
    mutable std::mutex storedMutex_;

    // Shard Index
    std::uint32_t const index_;

    // First ledger sequence in the shard
    std::uint32_t const firstSeq_;

    // Last ledger sequence in the shard
    std::uint32_t const lastSeq_;

    // The maximum number of ledgers the shard can store
    // The earliest shard may store fewer ledgers than subsequent shards
    std::uint32_t const maxLedgers_;

    // Path to database files
    boost::filesystem::path const dir_;

    // Storage space utilized by the shard
    std::uint64_t fileSz_{0};

    // Number of file descriptors required by the shard
    std::uint32_t fdRequired_{0};

    // NuDB key/value store for node objects
    std::unique_ptr<Backend> backend_;

    std::atomic<std::uint32_t> backendCount_{0};

    // Ledger SQLite database used for indexes
    std::unique_ptr<DatabaseCon> lgrSQLiteDB_;

    // Transaction SQLite database used for indexes
    std::unique_ptr<DatabaseCon> txSQLiteDB_;

    // Tracking information used only when acquiring a shard from the network.
    // If the shard is final, this member will be null.
    std::unique_ptr<AcquireInfo> acquireInfo_;

    // Older shard without an acquire database or final key
    // Eventually there will be no need for this and should be removed
    bool legacy_{false};

    // Determines if the shard needs to stop processing for shutdown
    std::atomic<bool> stop_{false};

    // Determines if the shard busy with replacing by deterministic one
    std::atomic<bool> busy_{false};

    std::atomic<State> state_{State::acquire};

    // Determines if the shard directory should be removed in the destructor
    std::atomic<bool> removeOnDestroy_{false};

    // The time of the last access of a shard that has a final state
    std::chrono::steady_clock::time_point lastAccess_;

    // Open shard databases
    [[nodiscard]] bool
    open(std::lock_guard<std::mutex> const& lock);

    // Open/Create SQLite databases
    // Lock over mutex_ required
    [[nodiscard]] bool
    initSQLite(std::lock_guard<std::mutex> const&);

    // Write SQLite entries for this ledger
    [[nodiscard]] bool
    storeSQLite(std::shared_ptr<Ledger const> const& ledger);

    // Set storage and file descriptor usage stats
    // Lock over mutex_ required
    void
    setFileStats(std::lock_guard<std::mutex> const&);

    // Verify this ledger by walking its SHAMaps and verifying its Merkle trees
    // Every node object verified will be stored in the deterministic shard
    [[nodiscard]] bool
    verifyLedger(
        std::shared_ptr<Ledger const> const& ledger,
        std::shared_ptr<Ledger const> const& next,
        std::shared_ptr<DeterministicShard> const& dShard) const;

    // Fetches from backend and log errors based on status codes
    [[nodiscard]] std::shared_ptr<NodeObject>
    verifyFetch(uint256 const& hash) const;

    // Open databases if they are closed
    [[nodiscard]] Shard::Count
    makeBackendCount();
};

}  // namespace NodeStore
}  // namespace ripple

#endif
