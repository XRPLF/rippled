#pragma once

#include <xrpl/nodestore/Scheduler.h>

namespace xrpl::NodeStore {

/** Simple NodeStore Scheduler that just performs the tasks synchronously. */
class DummyScheduler : public Scheduler
{
public:
    DummyScheduler() = default;
    ~DummyScheduler() override = default;
    void
    scheduleTask(Task& task) override;
    void
    onFetch(FetchReport const& report) override;
    void
    onBatchWrite(BatchWriteReport const& report) override;
};

}  // namespace xrpl::NodeStore
