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
#include <ripple/nodestore/NodeObject.h>
#include <ripple/nodestore/Scheduler.h>

#include <boost/filesystem.hpp>
#include <boost/serialization/map.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace ripple {
namespace NodeStore {

using PCache = TaggedCache<uint256, NodeObject>;
using NCache = KeyCache<uint256>;

/* A range of historical ledgers backed by a nodestore.
   Shards are indexed and store `ledgersPerShard`.
   Shard `i` stores ledgers starting with sequence: `1 + (i * ledgersPerShard)`
   and ending with sequence: `(i + 1) * ledgersPerShard`.
   Once a shard has all its ledgers, it is never written to again.
*/
class Shard
{
public:
    Shard(std::uint32_t index, int cacheSz,
        PCache::clock_type::rep cacheAge,
        beast::Journal& j);

    bool
    open(Section config, Scheduler& scheduler,
        boost::filesystem::path dir);

    bool
    setStored(std::shared_ptr<Ledger const> const& l);

    boost::optional<std::uint32_t>
    prepare();

    bool
    contains(std::uint32_t seq) const;

    void
    validate(Application& app);

    std::uint32_t
    index() const {return index_;}

    bool
    complete() const {return complete_;}

    std::shared_ptr<PCache>&
    pCache() {return pCache_;}

    std::shared_ptr<NCache>&
    nCache() {return nCache_;}

    std::uint64_t
    fileSize() const {return fileSize_;}

    std::shared_ptr<Backend> const&
    getBackend() const
    {
        assert(backend_);
        return backend_;
    }

    std::uint32_t
    fdlimit() const
    {
        assert(backend_);
        return backend_->fdlimit();
    }

    std::shared_ptr<Ledger const>
    lastStored() {return lastStored_;}

private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & storedSeqs_;
    }

    static constexpr auto controlFileName = "control.txt";

    // Shard Index
    std::uint32_t const index_;

    // First ledger sequence in this shard
    std::uint32_t const firstSeq_;

    // Last ledger sequence in this shard
    std::uint32_t const lastSeq_;

    // Database positive cache
    std::shared_ptr<PCache> pCache_;

    // Database negative cache
    std::shared_ptr<NCache> nCache_;

    std::uint64_t fileSize_ {0};
    std::shared_ptr<Backend> backend_;
    beast::Journal j_;

    // Path to database files
    boost::filesystem::path dir_;

    // True if shard has its entire ledger range stored
    bool complete_ {false};

    // Sequences of ledgers stored with an incomplete shard
    RangeSet<std::uint32_t> storedSeqs_;

    // Path to control file
    boost::filesystem::path control_;

    // Used as an optimization for visitDifferences
    std::shared_ptr<Ledger const> lastStored_;

    // Validate this ledger by walking its SHAMaps
    // and verifying each merkle tree
    bool
    valLedger(std::shared_ptr<Ledger const> const& l,
        std::shared_ptr<Ledger const> const& next);

    // Fetches from the backend and will log
    // errors based on status codes
    std::shared_ptr<NodeObject>
    valFetch(uint256 const& hash);

    // Calculate the file foot print of the backend files
    void
    updateFileSize();

    // Save the control file for an incomplete shard
    bool
    saveControl();
};

} // NodeStore
} // ripple

#endif
