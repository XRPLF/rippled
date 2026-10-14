#pragma once

#include <xrpl/core/JobQueue.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>

namespace xrpl {

/**
 * A node_store::Scheduler which uses the JobQueue.
 */
class NodeStoreScheduler : public node_store::Scheduler
{
public:
    explicit NodeStoreScheduler(JobQueue& jobQueue);

    void
    scheduleTask(node_store::Task& task) override;
    void
    onFetch(node_store::FetchReport const& report) override;
    void
    onBatchWrite(node_store::BatchWriteReport const& report) override;

private:
    JobQueue& jobQueue_;
};

}  // namespace xrpl
