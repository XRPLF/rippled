//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SHAMAP_TESTS_COMMON_H_INCLUDED
#define RIPPLE_SHAMAP_TESTS_COMMON_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/shamap/Family.h>

namespace ripple {
namespace tests {

class TestFamily : public Family
{
private:
    TestStopwatch clock_;
    NodeStore::DummyScheduler scheduler_;
    TreeNodeCache treecache_;
    FullBelowCache fullbelow_;
    RootStoppable parent_;
    std::unique_ptr<NodeStore::Database> db_;
    bool shardBacked_;
    beast::Journal j_;

public:
    TestFamily (beast::Journal j)
        : treecache_ ("TreeNodeCache", 65536, 60, clock_, j)
        , fullbelow_ ("full_below", clock_)
        , parent_ ("TestRootStoppable")
        , j_ (j)
    {
        Section testSection;
        testSection.set("type", "memory");
        testSection.set("Path", "SHAMap_test");
        db_ = NodeStore::Manager::instance ().make_Database (
            "test", scheduler_, 1, parent_, testSection, j);
        shardBacked_ =
            dynamic_cast<NodeStore::DatabaseShard*>(db_.get()) != nullptr;
    }

    beast::manual_clock <std::chrono::steady_clock>
    clock()
    {
        return clock_;
    }

    beast::Journal const&
    journal() override
    {
        return j_;
    }

    FullBelowCache&
    fullbelow() override
    {
        return fullbelow_;
    }

    FullBelowCache const&
    fullbelow() const override
    {
        return fullbelow_;
    }

    TreeNodeCache&
    treecache() override
    {
        return treecache_;
    }

    TreeNodeCache const&
    treecache() const override
    {
        return treecache_;
    }

    NodeStore::Database&
    db() override
    {
        return *db_;
    }

    NodeStore::Database const&
    db() const override
    {
        return *db_;
    }

    bool
    isShardBacked() const override
    {
        return shardBacked_;
    }

    void
    missing_node (std::uint32_t refNum) override
    {
        Throw<std::runtime_error> ("missing node");
    }

    void
    missing_node (uint256 const& refHash, std::uint32_t refNum) override
    {
        Throw<std::runtime_error> ("missing node");
    }

    void
    reset() override
    {
        fullbelow_.reset();
        treecache_.reset();
    }
};

} // tests
} // ripple

#endif
