#ifndef XRPL_NODESTORE_DUMMYSCHEDULER_H_INCLUDED
#define XRPL_NODESTORE_DUMMYSCHEDULER_H_INCLUDED

#include <xrpl/nodestore/Scheduler.h>

namespace ripple {
namespace NodeStore {

/** Simple NodeStore Scheduler that just peforms the tasks synchronously. */
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
}  // namespace ripple

#endif
