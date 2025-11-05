#ifndef XRPL_CORE_JOBTYPEINFO_H_INCLUDED
#define XRPL_CORE_JOBTYPEINFO_H_INCLUDED

#include <xrpld/core/Job.h>

namespace ripple {

/** Holds all the 'static' information about a job, which does not change */
class JobTypeInfo
{
private:
    JobType const m_type;
    std::string const m_name;

    /** The limit on the number of running jobs for this job type.

        A limit of 0 marks this as a "special job" which is not
        dispatched via the job queue.
     */
    int const m_limit;

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
        std::chrono::milliseconds avgLatency,
        std::chrono::milliseconds peakLatency)
        : m_type(type)
        , m_name(std::move(name))
        , m_limit(limit)
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
        return m_limit == 0;
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
