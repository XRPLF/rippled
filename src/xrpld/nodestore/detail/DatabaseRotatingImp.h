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

#ifndef RIPPLE_NODESTORE_DATABASEROTATINGIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASEROTATINGIMP_H_INCLUDED

#include <xrpld/nodestore/DatabaseRotating.h>

#include <shared_mutex>

namespace ripple {
namespace NodeStore {

class DatabaseRotatingImp : public DatabaseRotating
{
public:
    DatabaseRotatingImp() = delete;
    DatabaseRotatingImp(DatabaseRotatingImp const&) = delete;
    DatabaseRotatingImp&
    operator=(DatabaseRotatingImp const&) = delete;

    DatabaseRotatingImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> writableBackend,
        std::shared_ptr<Backend> archiveBackend,
        Section const& config,
        beast::Journal j);

    ~DatabaseRotatingImp()
    {
        stop();
    }

    [[nodiscard]] bool
    rotateWithLock(
        std::function<std::unique_ptr<NodeStore::Backend>(
            std::string const& writableBackendName)> const& f) override;

    std::string
    getName() const override;

    std::int32_t
    getWriteLoad() const override;

    void
    importDatabase(Database& source) override;

    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        // rotating store acts as one logical database
        return true;
    }

    void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
        override;

    void
    sync() override;

    void
    sweep() override;

    // Include the space in the name to ensure it can't be set in a file
    static constexpr auto unitTestFlag = " unit_test";

private:
    bool rotating = false;
    std::shared_ptr<Backend> writableBackend_;
    std::shared_ptr<Backend> archiveBackend_;
    bool const unitTest_;

    // https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/lock
    // "Shared mutexes do not support direct transition from shared to unique
    // ownership mode: the shared lock has to be relinquished with
    // unlock_shared() before exclusive ownership may be obtained with lock()."
    mutable std::shared_timed_mutex mutex_;

    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t,
        FetchReport& fetchReport,
        bool duplicate) override;

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
