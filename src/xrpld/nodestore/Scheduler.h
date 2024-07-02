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

#ifndef RIPPLE_NODESTORE_SCHEDULER_H_INCLUDED
#define RIPPLE_NODESTORE_SCHEDULER_H_INCLUDED

#include <ripple/nodestore/Task.h>
#include <chrono>

namespace ripple {
namespace NodeStore {

enum class FetchType { synchronous, async };

/** Contains information about a fetch operation. */
struct FetchReport
{
    explicit FetchReport(FetchType fetchType_) : fetchType(fetchType_)
    {
    }

    std::chrono::milliseconds elapsed;
    FetchType const fetchType;
    bool wasFound = false;
};

/** Contains information about a batch write operation. */
struct BatchWriteReport
{
    explicit BatchWriteReport() = default;

    std::chrono::milliseconds elapsed;
    int writeCount;
};

/** Scheduling for asynchronous backend activity

    For improved performance, a backend has the option of performing writes
    in batches. These writes can be scheduled using the provided scheduler
    object.

    @see BatchWriter
*/
class Scheduler
{
public:
    virtual ~Scheduler() = default;

    /** Schedules a task.
        Depending on the implementation, the task may be invoked either on
        the current thread of execution, or an unspecified
       implementation-defined foreign thread.
    */
    virtual void
    scheduleTask(Task& task) = 0;

    /** Reports completion of a fetch
        Allows the scheduler to monitor the node store's performance
    */
    virtual void
    onFetch(FetchReport const& report) = 0;

    /** Reports the completion of a batch write
        Allows the scheduler to monitor the node store's performance
    */
    virtual void
    onBatchWrite(BatchWriteReport const& report) = 0;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
