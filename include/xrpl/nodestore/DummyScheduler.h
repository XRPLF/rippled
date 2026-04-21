#pragma once

#include <xrpl/nodestore/Scheduler.h>

namespace xrpl {
namespace NodeStore {

/** Simple NodeStore Scheduler that just performs the tasks synchronously. */
class DummyScheduler : public Scheduler
{
public:
    DummyScheduler() = default;
    ~DummyScheduler() = default;
    void
    scheduleTask(Task& task) override;
    void
    onFetch(FetchReport const& report) override;
    void
    onBatchWrite(BatchWriteReport const& report) override;
};

}  // namespace NodeStore
}  // namespace xrpl
