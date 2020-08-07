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
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/nodestore/Scheduler.h>

#include <boost/filesystem.hpp>
#include <nudb/nudb.hpp>

#include <atomic>
#include <tuple>

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

    bool
    open(Scheduler& scheduler, nudb::context& ctx);

    void
    closeAll();

    boost::optional<std::uint32_t>
    prepare();

    bool
    store(std::shared_ptr<Ledger const> const& ledger);

    bool
    containsLedger(std::uint32_t seq) const;

    void
    sweep();

    std::uint32_t
    index() const
    {
        return index_;
    }

    boost::filesystem::path const&
    getDir() const
    {
        return dir_;
    }

    std::tuple<
        std::shared_ptr<Backend>,
        std::shared_ptr<PCache>,
        std::shared_ptr<NCache>>
    getBackendAll() const;

    std::shared_ptr<Backend>
    getBackend() const;

    /** Returns `true` if all shard ledgers have been stored in the backend
     */
    bool
    isBackendComplete() const;

    std::shared_ptr<PCache>
    pCache() const;

    std::shared_ptr<NCache>
    nCache() const;

    /** Returns a pair where the first item describes the storage space
        utilized and the second item is the number of file descriptors required.
    */
    std::pair<std::uint64_t, std::uint32_t>
    fileInfo() const;

    /** Returns `true` if the shard is complete, validated, and immutable.
     */
    bool
    isFinal() const;

    /** Returns `true` if the shard is older, without final key data
     */
    bool
    isLegacy() const;

    /** Finalize shard by walking its ledgers and verifying each Merkle tree.

        @param writeSQLite If true, SQLite entries will be rewritten using
        verified backend data.
        @param referenceHash If present, this hash must match the hash
        of the last ledger in the shard.
    */
    bool
    finalize(
        bool const writeSQLite,
        boost::optional<uint256> const& referenceHash);

    void
    stop()
    {
        stop_ = true;
    }

    /** If called, the shard directory will be removed when
        the shard is destroyed.
    */
    void
    removeOnDestroy()
    {
        removeOnDestroy_ = true;
    }

    // Current shard version
    static constexpr std::uint32_t version{2};

    // The finalKey is a hard coded value of zero. It is used to store
    // finalizing shard data to the backend. The data contains a version,
    // last ledger's hash, and the first and last ledger sequences.
    static uint256 const finalKey;

private:
    struct AcquireInfo
    {
        // SQLite database to track information about what has been acquired
        std::unique_ptr<DatabaseCon> SQLiteDB;

        // Tracks the sequences of ledgers acquired and stored in the backend
        RangeSet<std::uint32_t> storedSeqs;
    };

    Application& app_;
    mutable std::recursive_mutex mutex_;

    // Shard Index
    std::uint32_t const index_;

    // First ledger sequence in the shard
    std::uint32_t const firstSeq_;

    // Last ledger sequence in the shard
    std::uint32_t const lastSeq_;

    // The maximum number of ledgers the shard can store
    // The earliest shard may store fewer ledgers than subsequent shards
    std::uint32_t const maxLedgers_;

    // Database positive cache
    std::shared_ptr<PCache> pCache_;

    // Database negative cache
    std::shared_ptr<NCache> nCache_;

    // Path to database files
    boost::filesystem::path const dir_;

    // Storage space utilized by the shard
    std::uint64_t fileSz_{0};

    // Number of file descriptors required by the shard
    std::uint32_t fdRequired_{0};

    // NuDB key/value store for node objects
    std::shared_ptr<Backend> backend_;

    // Ledger SQLite database used for indexes
    std::unique_ptr<DatabaseCon> lgrSQLiteDB_;

    // Transaction SQLite database used for indexes
    std::unique_ptr<DatabaseCon> txSQLiteDB_;

    // Tracking information used only when acquiring a shard from the network.
    // If the shard is final, this member will be null.
    std::unique_ptr<AcquireInfo> acquireInfo_;

    beast::Journal const j_;

    // True if backend has stored all ledgers pertaining to the shard
    bool backendComplete_{false};

    // Older shard without an acquire database or final key
    // Eventually there will be no need for this and should be removed
    bool legacy_{false};

    // True if the backend has a final key stored
    bool final_{false};

    // Determines if the shard needs to stop processing for shutdown
    std::atomic<bool> stop_{false};

    // Determines if the shard directory should be removed in the destructor
    std::atomic<bool> removeOnDestroy_{false};

    // Open/Create SQLite databases
    // Lock over mutex_ required
    bool
    initSQLite(std::lock_guard<std::recursive_mutex> const& lock);

    // Write SQLite entries for this ledger
    // Lock over mutex_ required
    bool
    storeSQLite(
        std::shared_ptr<Ledger const> const& ledger,
        std::lock_guard<std::recursive_mutex> const& lock);

    // Set storage and file descriptor usage stats
    // Lock over mutex_ required
    void
    setFileStats(std::lock_guard<std::recursive_mutex> const& lock);

    // Validate this ledger by walking its SHAMaps and verifying Merkle trees
    bool
    valLedger(
        std::shared_ptr<Ledger const> const& ledger,
        std::shared_ptr<Ledger const> const& next) const;

    // Fetches from backend and log errors based on status codes
    std::shared_ptr<NodeObject>
    valFetch(uint256 const& hash) const;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
