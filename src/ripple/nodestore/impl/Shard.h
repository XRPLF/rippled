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

namespace ripple {
namespace NodeStore {

// Removes a path in its entirety
inline static
bool
removeAll(boost::filesystem::path const& path, beast::Journal& j)
{
    try
    {
        boost::filesystem::remove_all(path);
    }
    catch (std::exception const& e)
    {
        JLOG(j.error()) <<
            "exception: " << e.what();
        return false;
    }
    return true;
}

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
class Shard
{
public:
    Shard(
        Application& app,
        DatabaseShard const& db,
        std::uint32_t index,
        beast::Journal& j);

    bool
    open(Scheduler& scheduler, nudb::context& ctx);

    bool
    setStored(std::shared_ptr<Ledger const> const& ledger);

    boost::optional<std::uint32_t>
    prepare();

    bool
    contains(std::uint32_t seq) const;

    void
    sweep();

    std::uint32_t
    index() const
    {
        return index_;
    }

    std::shared_ptr<Backend> const&
    getBackend() const;

    bool
    complete() const;

    std::shared_ptr<PCache>
    pCache() const;

    std::shared_ptr<NCache>
    nCache() const;

    std::uint64_t
    fileSize() const;

    std::uint32_t
    fdRequired() const;

    std::shared_ptr<Ledger const>
    lastStored() const;

    bool
    validate() const;

private:
    static constexpr auto controlFileName = "control.txt";

    Application& app_;
    mutable std::mutex mutex_;

    // Shard Index
    std::uint32_t const index_;

    // First ledger sequence in this shard
    std::uint32_t const firstSeq_;

    // Last ledger sequence in this shard
    std::uint32_t const lastSeq_;

    // The maximum number of ledgers this shard can store
    // The earliest shard may store less ledgers than
    // subsequent shards
    std::uint32_t const maxLedgers_;

    // Database positive cache
    std::shared_ptr<PCache> pCache_;

    // Database negative cache
    std::shared_ptr<NCache> nCache_;

    // Path to database files
    boost::filesystem::path const dir_;

    // Path to control file
    boost::filesystem::path const control_;

    // Storage space utilized by the shard
    std::uint64_t fileSz_;

    // Number of file descriptors required by the shard
    std::uint32_t fdRequired_;

    // NuDB key/value store for node objects
    std::shared_ptr<Backend> backend_;

    // Ledger SQLite database used for indexes
    std::unique_ptr<DatabaseCon> lgrSQLiteDB_;

    // Transaction SQLite database used for indexes
    std::unique_ptr<DatabaseCon> txSQLiteDB_;

    beast::Journal j_;

    // True if shard has its entire ledger range stored
    bool complete_ {false};

    // Sequences of ledgers stored with an incomplete shard
    RangeSet<std::uint32_t> storedSeqs_;

    // Used as an optimization for visitDifferences
    std::shared_ptr<Ledger const> lastStored_;

    // Marks shard immutable
    // Lock over mutex_ required
    bool
    setComplete(std::lock_guard<std::mutex> const& lock);

    // Set the backend cache
    // Lock over mutex_ required
    void
    setCache(std::lock_guard<std::mutex> const& lock);

    // Open/Create SQLite databases
    // Lock over mutex_ required
    bool
    initSQLite(std::lock_guard<std::mutex> const& lock);

    // Write SQLite entries for a ledger stored in this shard's backend
    // Lock over mutex_ required
    bool
    setSQLiteStored(
        std::shared_ptr<Ledger const> const& ledger,
        std::lock_guard<std::mutex> const& lock);

    // Set storage and file descriptor usage stats
    // Lock over mutex_ required
    bool
    setFileStats(std::lock_guard<std::mutex> const& lock);

    // Save the control file for an incomplete shard
    // Lock over mutex_ required
    bool
    saveControl(std::lock_guard<std::mutex> const& lock);

    // Validate this ledger by walking its SHAMaps
    // and verifying each merkle tree
    bool
    valLedger(
        std::shared_ptr<Ledger const> const& ledger,
        std::shared_ptr<Ledger const> const& next) const;

    // Fetches from the backend and will log
    // errors based on status codes
    std::shared_ptr<NodeObject>
    valFetch(uint256 const& hash) const;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
