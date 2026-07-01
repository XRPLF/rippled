#pragma once

#include <xrpl/core/Job.h>

#include <chrono>
#include <string>
#include <utility>

namespace xrpl {

/** Holds all the 'static' information about a job, which does not change */
class JobTypeInfo
{
private:
    JobType const type_;
    std::string const name_;

    /** The limit on the number of running jobs for this job type.

        A limit of 0 marks this as a "special job" which is not
        dispatched via the job queue.
     */
    int const limit_;

    /** Average and peak latencies for this job type. 0 is none specified */
    std::chrono::milliseconds const avgLatency_;
    std::chrono::milliseconds const peakLatency_;

public:
    // Not default constructible
    JobTypeInfo() = delete;

    JobTypeInfo(
        JobType type,
        std::string name,
        int limit,
        std::chrono::milliseconds avgLatency,
        std::chrono::milliseconds peakLatency)
        : type_(type)
        , name_(std::move(name))
        , limit_(limit)
        , avgLatency_(avgLatency)
        , peakLatency_(peakLatency)
    {
    }

    [[nodiscard]] JobType
    type() const
    {
        return type_;
    }

    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    [[nodiscard]] int
    limit() const
    {
        return limit_;
    }

    [[nodiscard]] bool
    special() const
    {
        return limit_ == 0;
    }

    [[nodiscard]] std::chrono::milliseconds
    getAverageLatency() const
    {
        return avgLatency_;
    }

    [[nodiscard]] std::chrono::milliseconds
    getPeakLatency() const
    {
        return peakLatency_;
    }
};

}  // namespace xrpl
