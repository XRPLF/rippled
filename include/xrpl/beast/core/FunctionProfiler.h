
#ifndef RIPPLE_BASICS_FUNCTIONPROFILER_H_INCLUDED
#define RIPPLE_BASICS_FUNCTIONPROFILER_H_INCLUDED

#include <chrono>
#include <csignal>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <unordered_map>
#include <x86intrin.h>

namespace beast {

void
logProfilingResults();

class FunctionProfiler
{
public:
    std::string functionName;
    std::chrono::steady_clock::time_point start;
    std::uint64_t cpuCycleStart;
    inline static std::mutex mutex_;

    struct StatisticData
    {
        std::chrono::nanoseconds timeInTotal;
        std::uint64_t cpuCyclesInTotal;
        std::int64_t count;
    };

    inline static std::unordered_map<
        std::string,
        StatisticData>
        funcionDurations;
    FunctionProfiler(
        std::string const& tag,
        std::source_location location = std::source_location::current())
        : functionName(location.function_name() + tag)
        , start(std::chrono::steady_clock::now())
        , cpuCycleStart(__rdtsc())
    {
    }

    ~FunctionProfiler() noexcept
    {
        auto duration = std::chrono::steady_clock::now() - start;
        std::lock_guard<std::mutex> lock{mutex_};
        if (funcionDurations[functionName].count ==
            std::numeric_limits<std::int64_t>::max())
        {
            return;
        }
        funcionDurations[functionName].timeInTotal += duration;
        funcionDurations[functionName].cpuCyclesInTotal += (__rdtsc() - cpuCycleStart);
        funcionDurations[functionName].count++;
    }
};

inline std::string
getProfilingResults()
{
    std::lock_guard<std::mutex> lock{FunctionProfiler::mutex_};
    std::stringstream ss;
    ss << "Function profiling results:" << std::endl;
    ss << "name,time,cpu cycles,count" << std::endl;
    for (auto const& [name, duration] : FunctionProfiler::funcionDurations)
    {
        ss << name << "," << duration.timeInTotal.count() << ","
            << duration.cpuCyclesInTotal << ","
           << duration.count << std::endl;
    }

    return ss.str();
}

}  // namespace beast

#endif
