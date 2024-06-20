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

#ifndef RIPPLE_CORE_JOBTYPEDATA_H_INCLUDED
#define RIPPLE_CORE_JOBTYPEDATA_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/beast/insight/Collector.h>
#include <ripple/core/JobTypeInfo.h>

namespace ripple {

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
    int waiting;

    /* The number presently running */
    int running;

    /* And the number we deferred executing because of job limits */
    int deferred;

    /* Notification callbacks */
    beast::insight::Event dequeue;
    beast::insight::Event execute;

    JobTypeData(
        JobTypeInfo const& info_,
        beast::insight::Collector::ptr const& collector,
        Logs& logs) noexcept
        : m_load(logs.journal("LoadMonitor"))
        , m_collector(collector)
        , info(info_)
        , waiting(0)
        , running(0)
        , deferred(0)
    {
        m_load.setTargetLatency(
            info.getAverageLatency(), info.getPeakLatency());

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
        return m_load;
    }

    LoadMonitor::Stats
    stats()
    {
        return m_load.getStats();
    }
};

}  // namespace ripple

#endif
