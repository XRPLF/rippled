#pragma once

#include <xrpl/nodestore/Task.h>

#include <chrono>

namespace xrpl::node_store {

enum class FetchType { Synchronous, Async };

/**
 * Contains information about a fetch operation.
 */
struct FetchReport
{
    explicit FetchReport(FetchType fetchType) : fetchType(fetchType)
    {
    }

    /**
     * Wall time the fetch took, in microseconds.
     *
     * Microseconds and not milliseconds: a warm nodestore answers a read in
     * single-digit microseconds and a cold one in low hundreds. A
     * millisecond field rounds both to zero, so it discards the only
     * quantity that separates a healthy read path from a stalled one.
     *
     * A consumer that needs milliseconds casts explicitly.
     */
    std::chrono::microseconds elapsed{};
    FetchType const fetchType;
    bool wasFound = false;
};

/**
 * Contains information about a batch write operation.
 */
struct BatchWriteReport
{
    explicit BatchWriteReport() = default;

    /**
     * Wall time the batch write took, in milliseconds.
     *
     * Milliseconds is the right unit here, unlike on FetchReport: a batch
     * write covers many objects and reaches the disk, so it lands in the
     * millisecond range rather than below it.
     */
    std::chrono::milliseconds elapsed;

    /**
     * Number of objects written in the batch.
     */
    int writeCount;
};

/**
 * Scheduling for asynchronous backend activity
 *
 * For improved performance, a backend has the option of performing writes
 * in batches. These writes can be scheduled using the provided scheduler
 * object.
 *
 * @see BatchWriter
 */
class Scheduler
{
public:
    virtual ~Scheduler() = default;

    /**
     * Schedules a task.
     *  Depending on the implementation, the task may be invoked either on
     *  the current thread of execution, or an unspecified
     * implementation-defined foreign thread.
     */
    virtual void
    scheduleTask(Task& task) = 0;

    /**
     * Reports completion of a fetch
     * Allows the scheduler to monitor the node store's performance
     */
    virtual void
    onFetch(FetchReport const& report) = 0;

    /**
     * Reports the completion of a batch write
     * Allows the scheduler to monitor the node store's performance
     */
    virtual void
    onBatchWrite(BatchWriteReport const& report) = 0;
};

}  // namespace xrpl::node_store
