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

#ifndef RIPPLE_CORE_JOBTYPEINFO_H_INCLUDED
#define RIPPLE_CORE_JOBTYPEINFO_H_INCLUDED

#include <ripple/core/Job.h>

namespace ripple {

/** Holds all the 'static' information about a job, which does not change */
class JobTypeInfo
{
private:
    JobType const m_type;
    std::string const m_name;

    /** The limit on the number of running jobs for this job type. */
    int const m_limit;

    /** Special jobs are not dispatched via the job queue */
    bool const m_special;

    /** Average and peak latencies for this job type. 0 is none specified */
    std::chrono::milliseconds const m_avgLatency;
    std::chrono::milliseconds const m_peakLatency;

public:
    // Not default constructible
    JobTypeInfo() = delete;

    JobTypeInfo(
        JobType type,
        std::string name,
        int limit,
        bool special,
        std::chrono::milliseconds avgLatency,
        std::chrono::milliseconds peakLatency)
        : m_type(type)
        , m_name(std::move(name))
        , m_limit(limit)
        , m_special(special)
        , m_avgLatency(avgLatency)
        , m_peakLatency(peakLatency)
    {
    }

    JobType
    type() const
    {
        return m_type;
    }

    std::string const&
    name() const
    {
        return m_name;
    }

    int
    limit() const
    {
        return m_limit;
    }

    bool
    special() const
    {
        return m_special;
    }

    std::chrono::milliseconds
    getAverageLatency() const
    {
        return m_avgLatency;
    }

    std::chrono::milliseconds
    getPeakLatency() const
    {
        return m_peakLatency;
    }
};

}  // namespace ripple

#endif
