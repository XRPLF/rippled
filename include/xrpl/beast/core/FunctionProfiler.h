
#ifndef RIPPLE_BASICS_FUNCTIONPROFILER_H_INCLUDED
#define RIPPLE_BASICS_FUNCTIONPROFILER_H_INCLUDED

#include <chrono>
#include <csignal>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <unordered_map>
// #include <x86intrin.h>
#include <vector>
#include <cmath>
#include <numeric>  // std::accumulate

// #define __rdtsc() 0

namespace beast {

template <typename T>
double compute_stddev(const std::vector<T>& samples) {
    if (samples.size() < 2) return 0.0;

    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    double sum_sq = 0.0;

    for (double x : samples) {
        sum_sq += (x - mean) * (x - mean);
    }

    return std::sqrt(sum_sq / (samples.size() - 1));
}

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
        std::vector<std::chrono::nanoseconds> time;
        std::vector<std::uint64_t> cpuCycles;
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
        funcionDurations[functionName].time.emplace_back(duration);
        funcionDurations[functionName].cpuCycles.emplace_back((__rdtsc() - cpuCycleStart));
    }
};

inline std::string
getProfilingResults()
{
    std::lock_guard<std::mutex> lock{FunctionProfiler::mutex_};
    std::stringstream ss;
    ss << "Function profiling results:" << std::endl;
    ss << "name,time,cpu cycles,count,average time(ns),time standard deviation,average cpu cycles,cpu cycles standard deviation" << std::endl;
    for (auto const& [name, duration] : FunctionProfiler::funcionDurations)
    {
        std::vector<std::int64_t> times;
        times.reserve(duration.time.size());

        std::transform(std::begin(duration.time), std::end(duration.time), std::back_inserter(times), [](const std::chrono::nanoseconds& time) {
            return static_cast<std::int64_t>(time.count());
        });

        auto timeInTotal = std::accumulate(std::begin(times), std::end(times), std::int64_t{0});
        auto cpuCyclesInTotal = std::accumulate(std::begin(duration.cpuCycles), std::end(duration.cpuCycles), std::int64_t{0});

        ss << name << "," << timeInTotal << ","
            << cpuCyclesInTotal << ","
            << duration.time.size() << ","
            << timeInTotal / (double)duration.time.size() << ","
            << compute_stddev(times) << ","
            << cpuCyclesInTotal / (double)duration.cpuCycles.size() << ","
            << compute_stddev(duration.cpuCycles)
            << std::endl;
    }

    return ss.str();
}

}  // namespace beast

#endif
