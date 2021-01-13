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

class TestNodeFamily : public Family
{
private:
    std::unique_ptr<NodeStore::Database> db_;

    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;

    TestStopwatch clock_;
    NodeStore::DummyScheduler scheduler_;
    RootStoppable parent_;

    beast::Journal const j_;

public:
    TestNodeFamily(beast::Journal j)
        : fbCache_(std::make_shared<FullBelowCache>(
              "App family full below cache",
              clock_))
        , tnCache_(std::make_shared<TreeNodeCache>(
              "App family tree node cache",
              65536,
              std::chrono::minutes{1},
              clock_,
              j))
        , parent_("TestRootStoppable")
        , j_(j)
    {
        Section testSection;
        testSection.set("type", "memory");
        testSection.set("Path", "SHAMap_test");
        db_ = NodeStore::Manager::instance().make_Database(
            "test", megabytes(4), scheduler_, 1, parent_, testSection, j);
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

    beast::Journal const&
    journal() override
    {
        return j_;
    }

    std::shared_ptr<FullBelowCache> getFullBelowCache(std::uint32_t) override
    {
        return fbCache_;
    }

    std::shared_ptr<TreeNodeCache> getTreeNodeCache(std::uint32_t) override
    {
        return tnCache_;
    }

    void
    sweep() override
    {
        fbCache_->sweep();
        tnCache_->sweep();
    }

    bool
    isShardBacked() const override
    {
        return true;
    }

    void
    missingNode(std::uint32_t refNum) override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    missingNode(uint256 const& refHash, std::uint32_t refNum) override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    reset() override
    {
        fbCache_->reset();
        tnCache_->reset();
    }

    beast::manual_clock<std::chrono::steady_clock>
    clock()
    {
        return clock_;
    }
};

}  // namespace tests
}  // namespace ripple

#endif
