//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_DETERMINISTICSHARD_H_INCLUDED
#define RIPPLE_NODESTORE_DETERMINISTICSHARD_H_INCLUDED

#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <nudb/nudb.hpp>
#include <set>

namespace ripple {
namespace NodeStore {

/** DeterministicShard class.
 *
 * 1. The init() method creates temporary folder tempdir_,
 *    and the deterministic shard is initialized in that folder.
 * 2. The store() method adds object to memory pool.
 * 3. The flush() method stores all objects from memory pool to the shard
 *    located in tempdir_ in sorted order.
 * 4. The close(true) method finalizes the shard and moves it from tempdir_
 *    temporary folder to filandir_ permanent folder,
 *    deleting old (non-deterministic) shard located in finaldir_.
 */
class DeterministicShard
{
public:
    using nodeptr = std::shared_ptr<NodeObject>;

    DeterministicShard(DeterministicShard const&) = delete;
    DeterministicShard&
    operator=(DeterministicShard const&) = delete;

    /** Creates the object for shard database
     *
     * @param app Application object
     * @param db Shard Database which deterministic shard belongs to
     * @param index Index of the shard
     * @param lastHash Hash of last ledger in the shard
     * @param j Journal to logging
     */
    DeterministicShard(
        Application& app,
        DatabaseShard const& db,
        std::uint32_t index,
        uint256 const& lastHash,
        beast::Journal j);

    ~DeterministicShard();

    /** Initializes the deterministic shard.
     *
     * @return true is success, false if errored
     */
    bool
    init();

    /** Finalizes and closes the shard.
     *
     * @param cancel True if reject the shard and delete all files,
     *               false if finalize the shard and store them
     */
    void
    close(bool cancel = false);

    /** Store the object into memory pool
     *
     * @param nobj Object to store.
     */
    void
    store(nodeptr nobj);

    /** Flush all objects from memory pool to shard
     */
    void
    flush();

private:
    // Count hash of shard parameters: lashHash, firstSeq, lastSeq, index
    uint160
    hash(const uint256& lastHash) const;

    // Get n-th 64-bit portion of shard parameters's hash
    std::uint64_t
    digest(int n) const;

    // If database inited
    bool inited_;

    // Sorted set of stored and not flushed objects
    std::set<nodeptr, std::function<bool(nodeptr, nodeptr)>> nodeset_;

    // Application reference
    Application& app_;

    // Shard database
    DatabaseShard const& db_;

    // Shard Index
    std::uint32_t const index_;

    // Hash used for digests
    uint160 const hash_;

    // Path to temporary database files
    boost::filesystem::path const tempdir_;

    // Path to final database files
    boost::filesystem::path const finaldir_;

    // Dummy scheduler for deterministic write
    DummyScheduler scheduler_;

    // NuDB context
    std::unique_ptr<nudb::context> ctx_;

    // NuDB key/value store for node objects
    std::shared_ptr<Backend> backend_;

    // Journal
    beast::Journal const j_;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
