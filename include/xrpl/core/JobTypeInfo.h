#pragma once

#include <xrpl/core/Job.h>

namespace xrpl {

/** Holds all the 'static' information about a job, which does not change */
class JobTypeInfo
{
private:
    JobType const m_type_;
    std::string const m_name_;

    /** The limit on the number of running jobs for this job type.

        A limit of 0 marks this as a "special job" which is not
        dispatched via the job queue.
     */
    int const m_limit_;

    /** Average and peak latencies for this job type. 0 is none specified */
    std::chrono::milliseconds const m_avgLatency_;
    std::chrono::milliseconds const m_peakLatency_;

public:
    // Not default constructible
    JobTypeInfo() = delete;

    JobTypeInfo(
        JobType type,
        std::string name,
        int limit,
        std::chrono::milliseconds avgLatency,
        std::chrono::milliseconds peakLatency)
        : m_type_(type)
        , m_name_(std::move(name))
        , m_limit_(limit)
        , m_avgLatency_(avgLatency)
        , m_peakLatency_(peakLatency)
    {
    }

    [[nodiscard]] JobType
    type() const
    {
        return m_type_;
    }

    [[nodiscard]] std::string const&
    name() const
    {
        return m_name_;
    }

    [[nodiscard]] int
    limit() const
    {
        return m_limit_;
    }

    [[nodiscard]] bool
    special() const
    {
        return m_limit_ == 0;
    }

    [[nodiscard]] std::chrono::milliseconds
    getAverageLatency() const
    {
        return m_avgLatency_;
    }

    [[nodiscard]] std::chrono::milliseconds
    getPeakLatency() const
    {
        return m_peakLatency_;
    }
};

}  // namespace xrpl
