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

#ifndef RIPPLE_APP_MAIN_NODESTORESCHEDULER_H_INCLUDED
#define RIPPLE_APP_MAIN_NODESTORESCHEDULER_H_INCLUDED

#include <ripple/core/JobQueue.h>
#include <ripple/core/Stoppable.h>
#include <ripple/nodestore/Scheduler.h>
#include <atomic>

namespace ripple {

/** A NodeStore::Scheduler which uses the JobQueue and implements the Stoppable
 * API. */
class NodeStoreScheduler : public NodeStore::Scheduler, public Stoppable
{
public:
    NodeStoreScheduler(Stoppable& parent);

    // VFALCO NOTE This is a temporary hack to solve the problem
    //             of circular dependency.
    //
    void
    setJobQueue(JobQueue& jobQueue);

    void
    onStop() override;
    void
    onChildrenStopped() override;
    void
    scheduleTask(NodeStore::Task& task) override;
    void
    onFetch(NodeStore::FetchReport const& report) override;
    void
    onBatchWrite(NodeStore::BatchWriteReport const& report) override;

private:
    void
    doTask(NodeStore::Task& task);

    JobQueue* m_jobQueue{nullptr};
    std::atomic<int> m_taskCount{0};
};

}  // namespace ripple

#endif
