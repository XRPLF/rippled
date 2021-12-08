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
 * 1. The init() method creates temporary folder dir_,
 *    and the deterministic shard is initialized in that folder.
 * 2. The store() method adds object to memory pool.
 * 3. The flush() method stores all objects from memory pool to the shard
 *    located in dir_ in sorted order.
 * 4. The close(true) method closes the backend and removes the directory.
 */
class DeterministicShard
{
    constexpr static std::uint32_t maxMemObjsDefault = 16384u;
    constexpr static std::uint32_t maxMemObjsTest = 16u;

    /* "SHRD" in ASCII */
    constexpr static std::uint64_t deterministicType = 0x5348524400000000ll;

private:
    DeterministicShard(DeterministicShard const&) = delete;
    DeterministicShard&
    operator=(DeterministicShard const&) = delete;

    /** Creates the object for shard database
     *
     * @param app Application object
     * @param dir Directory where shard is located
     * @param index Index of the shard
     * @param j Journal to logging
     */
    DeterministicShard(
        Application& app,
        boost::filesystem::path const& dir,
        std::uint32_t index,
        beast::Journal j);

    /** Initializes the deterministic shard.
     *
     * @param finalKey Serializer of shard's final key which consists of:
     *        shard version (32 bit)
     *        first ledger sequence in the shard (32 bit)
     *        last ledger sequence in the shard (32 bit)
     *        hash of last ledger (256 bits)
     * @return true if no error, false if error
     */
    bool
    init(Serializer const& finalKey);

public:
    ~DeterministicShard();

    /** Finalizes and closes the shard.
     */
    void
    close()
    {
        close(false);
    }

    [[nodiscard]] boost::filesystem::path const&
    getDir() const
    {
        return dir_;
    }

    /** Store a node object in memory.
     *
     * @param nodeObject The node object to store
     * @return true on success.
     * @note Flushes all objects in memory to the backend when the number
     *       of node objects held in memory exceed a threshold
     */
    [[nodiscard]] bool
    store(std::shared_ptr<NodeObject> const& nodeObject);

private:
    /** Finalizes and closes the shard.
     *
     * @param cancel True if reject the shard and delete all files,
     *               false if finalize the shard and store them
     */
    void
    close(bool cancel);

    // Application reference
    Application& app_;

    // Shard Index
    std::uint32_t const index_;

    // Path to temporary database files
    boost::filesystem::path const dir_;

    // Dummy scheduler for deterministic write
    DummyScheduler scheduler_;

    // NuDB context
    std::unique_ptr<nudb::context> ctx_;

    // NuDB key/value store for node objects
    std::shared_ptr<Backend> backend_;

    // Journal
    beast::Journal const j_;

    // Current number of in-cache objects
    std::uint32_t curMemObjs_;

    // Maximum number of in-cache objects
    std::uint32_t const maxMemObjs_;

    friend std::shared_ptr<DeterministicShard>
    make_DeterministicShard(
        Application& app,
        boost::filesystem::path const& shardDir,
        std::uint32_t shardIndex,
        Serializer const& finalKey,
        beast::Journal j);
};

/** Creates shared pointer to deterministic shard and initializes it.
 *
 * @param app Application object
 * @param shardDir Directory where shard is located
 * @param shardIndex Index of the shard
 * @param finalKey Serializer of shard's ginal key which consists of:
 *        shard version (32 bit)
 *        first ledger sequence in the shard (32 bit)
 *        last ledger sequence in the shard (32 bit)
 *        hash of last ledger (256 bits)
 * @param j Journal to logging
 * @return Shared pointer to deterministic shard or {} in case of error.
 */
std::shared_ptr<DeterministicShard>
make_DeterministicShard(
    Application& app,
    boost::filesystem::path const& shardDir,
    std::uint32_t shardIndex,
    Serializer const& finalKey,
    beast::Journal j);

}  // namespace NodeStore
}  // namespace ripple

#endif
