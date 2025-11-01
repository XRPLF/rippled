#ifndef XRPL_APP_MAIN_NODESTORESCHEDULER_H_INCLUDED
#define XRPL_APP_MAIN_NODESTORESCHEDULER_H_INCLUDED

#include <xrpld/core/JobQueue.h>

#include <xrpl/nodestore/Scheduler.h>

namespace ripple {

/** A NodeStore::Scheduler which uses the JobQueue. */
class NodeStoreScheduler : public NodeStore::Scheduler
{
public:
    explicit NodeStoreScheduler(JobQueue& jobQueue);

    void
    scheduleTask(NodeStore::Task& task) override;
    void
    onFetch(NodeStore::FetchReport const& report) override;
    void
    onBatchWrite(NodeStore::BatchWriteReport const& report) override;

private:
    JobQueue& jobQueue_;
};

}  // namespace ripple

#endif
