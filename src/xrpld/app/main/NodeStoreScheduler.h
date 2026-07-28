#pragma once

#include <xrpl/core/JobQueue.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>

namespace xrpl {

// Forward-declared rather than included: only a reference is stored, and
// ServiceRegistry.h pulls in <boost/asio.hpp>, which every file including this
// header would then pay for. The .cpp includes the full definition.
class ServiceRegistry;

/**
 * A node_store::Scheduler which uses the JobQueue.
 *
 * Two responsibilities, both delegating outward: it turns backend write
 * requests into JobQueue jobs, and it forwards completion reports to the
 * load monitor and to the OTel metrics pipeline.
 *
 * Collaborator diagram (ASCII):
 *
 *   node_store::Database / Backend
 *            |
 *            | scheduleTask / onFetch / onBatchWrite
 *            v
 *   +---------------------+
 *   | NodeStoreScheduler  |
 *   +---------------------+
 *        |            |
 *        v            v
 *   JobQueue     ServiceRegistry
 *   (jobs +      (-> MetricsRegistry,
 *   load events)  read-latency histogram)
 *
 * @note Thread safety: onFetch() and onBatchWrite() are called from backend
 *       read/write threads, concurrently. Both members are references to
 *       objects that outlive this one, and every call they make
 *       (JobQueue::addLoadEvents, OTel Histogram::Record) is itself
 *       thread-safe, so no locking is needed here.
 * @note Lifetime: constructed early, in the Application member initializer
 *       list, which is BEFORE the MetricsRegistry exists. It therefore stores
 *       the ServiceRegistry and resolves the registry per call; the metric
 *       macros null-check it, so fetches completing before the registry is
 *       created are simply not recorded.
 */
class NodeStoreScheduler : public node_store::Scheduler
{
public:
    /**
     * Construct a scheduler.
     *
     * @param app       Service registry, used only to reach the
     *                  MetricsRegistry when reporting read latency. Must
     *                  outlive this object.
     * @param jobQueue  Queue that runs scheduled write tasks and receives
     *                  load events. Must outlive this object.
     */
    NodeStoreScheduler(ServiceRegistry& app, JobQueue& jobQueue);

    void
    scheduleTask(node_store::Task& task) override;
    void
    onFetch(node_store::FetchReport const& report) override;
    void
    onBatchWrite(node_store::BatchWriteReport const& report) override;

private:
#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Service registry, resolved to a MetricsRegistry on each onFetch().
     *
     * Only needed when OTel is compiled in, since the record site is the
     * only reader. Held under the guard for the same reason
     * MetricsRegistry::app_ is: without OTel it would be an unused private
     * field, which -Wall rejects.
     */
    ServiceRegistry& app_;
#endif  // XRPL_ENABLE_TELEMETRY

    /**
     * Queue used for scheduled tasks and load-event reporting.
     */
    JobQueue& jobQueue_;
};

}  // namespace xrpl
