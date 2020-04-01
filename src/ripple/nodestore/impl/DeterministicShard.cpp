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

#include <ripple/app/main/Application.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/DeterministicShard.h>
#include <ripple/nodestore/impl/Shard.h>
#include <ripple/protocol/digest.h>
#include <fstream>
#include <nudb/detail/format.hpp>
#include <nudb/nudb.hpp>
#include <openssl/ripemd.h>

namespace ripple {
namespace NodeStore {

DeterministicShard::DeterministicShard(
    Application& app,
    DatabaseShard const& db,
    std::uint32_t index,
    uint256 const& lastHash,
    beast::Journal j)
    : inited_(false)
    , nodeset_([](nodeptr l, nodeptr r) { return l->getHash() < r->getHash(); })
    , app_(app)
    , db_(db)
    , index_(index)
    , hash_(hash(lastHash))
    , tempdir_(db.getRootDir() / (std::to_string(index_) + ".tmp"))
    , finaldir_(db.getRootDir() / std::to_string(index_))
    , ctx_(std::make_unique<nudb::context>())
    , j_(j)
{
}

DeterministicShard::~DeterministicShard()
{
    close(true);
}

uint160
DeterministicShard::hash(uint256 const& lastHash) const
{
    using beast::hash_append;
    ripemd160_hasher h;

    hash_append(h, lastHash);
    hash_append(h, index_);
    hash_append(h, db_.firstLedgerSeq(index_));
    hash_append(h, db_.lastLedgerSeq(index_));
    hash_append(h, Shard::version);

    auto const result = static_cast<ripemd160_hasher::result_type>(h);
    return uint160::fromVoid(result.data());
}

std::uint64_t
DeterministicShard::digest(int n) const
{
    auto const data = hash_.data();

    if (n == 2)
    {  // Extract 32 bits:
        return (static_cast<std::uint64_t>(data[19]) << 24) +
            (static_cast<std::uint64_t>(data[18]) << 16) +
            (static_cast<std::uint64_t>(data[17]) << 8) +
            (static_cast<std::uint64_t>(data[16]));
    }

    std::uint64_t ret = 0;

    if (n == 0 || n == 1)
    {  // Extract 64 bits
        for (int i = n; i < 16; i += 2)
            ret = (ret << 8) + data[i];
    }

    return ret;
}

bool
DeterministicShard::init()
{
    if (index_ < db_.earliestShardIndex())
    {
        JLOG(j_.error()) << "shard " << index_ << " is illegal";
        return false;
    }

    Config const& config{app_.config()};

    Section section{config.section(ConfigSection::shardDatabase())};
    std::string const type{get<std::string>(section, "type", "nudb")};

    if (type != "nudb")
    {
        JLOG(j_.error()) << "shard " << index_ << " backend type " << type
                         << " not supported";
        return false;
    }

    auto factory{Manager::instance().find(type)};
    if (!factory)
    {
        JLOG(j_.error()) << "shard " << index_
                         << " failed to create factory for backend type "
                         << type;
        return false;
    }

    ctx_->start();

    section.set("path", tempdir_.string());
    backend_ = factory->createInstance(
        NodeObject::keyBytes, section, scheduler_, *ctx_, j_);

    if (!backend_)
    {
        JLOG(j_.error()) << "shard " << index_
                         << " failed to create backend type " << type;
        return false;
    }

    // Open or create the NuDB key/value store
    bool preexist = exists(tempdir_);
    if (preexist)
    {
        remove_all(tempdir_);
        preexist = false;
    }

    backend_->open(
        !preexist,
        digest(2) | 0x5348524400000000ll, /* appType */
        digest(0),                        /* uid */
        digest(1)                         /* salt */
    );

    inited_ = true;

    return true;
}

void
DeterministicShard::close(bool cancel)
{
    if (!inited_)
        return;

    backend_->close();
    if (cancel)
    {
        remove_all(tempdir_);
    }
    else
    {
        flush();
        remove_all(finaldir_);
        rename(tempdir_, finaldir_);
    }
    inited_ = false;
}

void
DeterministicShard::store(nodeptr nObj)
{
    if (!inited_)
        return;

    nodeset_.insert(nObj);
}

void
DeterministicShard::flush()
{
    if (!inited_)
        return;

    for (auto nObj : nodeset_)
    {
        backend_->store(nObj);
    }

    nodeset_.clear();
}

}  // namespace NodeStore
}  // namespace ripple
