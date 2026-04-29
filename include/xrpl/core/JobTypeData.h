#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/core/JobTypeInfo.h>

#include <utility>

namespace xrpl {

struct JobTypeData
{
private:
    LoadMonitor m_load;

    /* Support for insight */
    beast::insight::Collector::ptr m_collector;

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
        : m_load(logs.journal("LoadMonitor")), m_collector(std::move(collector)), info(info_)

    {
        m_load.setTargetLatency(info.getAverageLatency(), info.getPeakLatency());

        if (!info.special())
        {
            dequeue = m_collector->make_event(info.name() + "_q");
            execute = m_collector->make_event(info.name());
        }
    }

    /* Not copy-constructible or assignable */
    JobTypeData(JobTypeData const& other) = delete;
    JobTypeData&
    operator=(JobTypeData const& other) = delete;

    [[nodiscard]] std::string
    name() const
    {
        return info.name();
    }

    [[nodiscard]] JobType
    type() const
    {
        return info.type();
    }

    LoadMonitor&
    load()
    {
        return m_load;
    }

    LoadMonitor::Stats
    stats()
    {
        return m_load.getStats();
    }
};

}  // namespace xrpl
