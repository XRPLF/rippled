#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/core/JobTypeInfo.h>

#include <utility>

namespace xrpl {

struct JobTypeData
{
private:
    LoadMonitor load_;

    /* Support for insight */
    beast::insight::Collector::ptr collector_;

public:
    /* The job category which we represent */
    JobTypeInfo const& info;

    /* The number of jobs waiting */
    int waiting{0};

    /* The number presently running */
    int running{0};

    /* And the number we deferred executing because of job limits */
    int deferred{0};

    /* Notification callbacks */
    beast::insight::Event dequeue;
    beast::insight::Event execute;

    JobTypeData(
        JobTypeInfo const& info_,
        beast::insight::Collector::ptr collector,
        Logs& logs) noexcept
        : load_(logs.journal("LoadMonitor"))
        , collector_(collector)
        , info(info_)
        , waiting(0)
        , running(0)
        , deferred(0)
    {
        load_.setTargetLatency(info.getAverageLatency(), info.getPeakLatency());

        if (!info.special())
        {
            dequeue = collector_->make_event(info.name() + "_q");
            execute = collector_->make_event(info.name());
        }
    }

    /* Not copy-constructible or assignable */
    JobTypeData(JobTypeData const& other) = delete;
    JobTypeData&
    operator=(JobTypeData const& other) = delete;

    std::string
    name() const
    {
        return info.name();
    }

    JobType
    type() const
    {
        return info.type();
    }

    LoadMonitor&
    load()
    {
        return load_;
    }

    LoadMonitor::Stats
    stats()
    {
        return load_.getStats();
    }
};

}  // namespace xrpl
