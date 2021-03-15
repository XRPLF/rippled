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
    boost::filesystem::path const& dir,
    std::uint32_t index,
    beast::Journal j)
    : app_(app)
    , index_(index)
    , dir_(dir / "tmp")
    , ctx_(std::make_unique<nudb::context>())
    , j_(j)
    , curMemObjs_(0)
    , maxMemObjs_(
          app_.getShardStore()->ledgersPerShard() <= 256 ? maxMemObjsTest
                                                         : maxMemObjsDefault)
{
}

DeterministicShard::~DeterministicShard()
{
    close(true);
}

bool
DeterministicShard::init(Serializer const& finalKey)
{
    auto db = app_.getShardStore();

    auto fail = [&](std::string const& msg) {
        JLOG(j_.error()) << "deterministic shard " << index_
                         << " not created: " << msg;
        backend_.reset();
        try
        {
            remove_all(dir_);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "deterministic shard " << index_
                             << ". Exception caught in function " << __func__
                             << ". Error: " << e.what();
        }
        return false;
    };

    if (!db)
        return fail("shard store not exists");

    if (index_ < db->earliestShardIndex())
        return fail("Invalid shard index");

    Config const& config{app_.config()};
    Section section{config.section(ConfigSection::shardDatabase())};
    auto const type{get<std::string>(section, "type", "nudb")};
    auto const factory{Manager::instance().find(type)};
    if (!factory)
        return fail("failed to find factory for " + type);

    section.set("path", dir_.string());
    backend_ = factory->createInstance(
        NodeObject::keyBytes, section, 1, scheduler_, *ctx_, j_);

    if (!backend_)
        return fail("failed to create database");

    ripemd160_hasher h;
    h(finalKey.data(), finalKey.size());
    auto const result{static_cast<ripemd160_hasher::result_type>(h)};
    auto const hash{uint160::fromVoid(result.data())};

    auto digest = [&](int n) {
        auto const data{hash.data()};
        std::uint64_t result{0};

        switch (n)
        {
            case 0:
            case 1:
                // Construct 64 bits from sequential eight bytes
                for (int i = 0; i < 8; i++)
                    result = (result << 8) + data[n * 8 + i];
                break;

            case 2:
                // Construct 64 bits using the last four bytes of data
                result = (static_cast<std::uint64_t>(data[16]) << 24) +
                    (static_cast<std::uint64_t>(data[17]) << 16) +
                    (static_cast<std::uint64_t>(data[18]) << 8) +
                    (static_cast<std::uint64_t>(data[19]));
                break;
        }

        return result;
    };
    auto const uid{digest(0)};
    auto const salt{digest(1)};
    auto const appType{digest(2) | deterministicType};

    // Open or create the NuDB key/value store
    try
    {
        if (exists(dir_))
            remove_all(dir_);

        backend_->open(true, appType, uid, salt);
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

    return true;
}

std::shared_ptr<DeterministicShard>
make_DeterministicShard(
    Application& app,
    boost::filesystem::path const& shardDir,
    std::uint32_t shardIndex,
    Serializer const& finalKey,
    beast::Journal j)
{
    std::shared_ptr<DeterministicShard> dShard(
        new DeterministicShard(app, shardDir, shardIndex, j));
    if (!dShard->init(finalKey))
        return {};
    return dShard;
}

void
DeterministicShard::close(bool cancel)
{
    try
    {
        if (cancel)
        {
            backend_.reset();
            remove_all(dir_);
        }
        else
        {
            ctx_->flush();
            curMemObjs_ = 0;
            backend_.reset();
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "deterministic shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
    }
}

bool
DeterministicShard::store(std::shared_ptr<NodeObject> const& nodeObject)
{
    try
    {
        backend_->store(nodeObject);

        // Flush to the backend if at threshold
        if (++curMemObjs_ >= maxMemObjs_)
        {
            ctx_->flush();
            curMemObjs_ = 0;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "deterministic shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
        return false;
    }

    return true;
}

}  // namespace NodeStore
}  // namespace ripple
