
#ifndef RIPPLE_BASICS_FUNCTIONPROFILER_H_INCLUDED
#define RIPPLE_BASICS_FUNCTIONPROFILER_H_INCLUDED

#include <string>
#include <chrono>
#include <unordered_map>
#include <sstream>
#include <source_location>
#include <csignal>

namespace beast {

    void logProfilingResults();
class FunctionProfiler
{
    std::string functionName;
    std::chrono::steady_clock::time_point start;
public:

    inline static std::unordered_map<std::string, std::chrono::nanoseconds> funcionDurations;
    FunctionProfiler(std::source_location location = std::source_location::current()): functionName(location.function_name()), start(std::chrono::steady_clock::now()) 
    {
    }

    ~FunctionProfiler() noexcept
    {
        auto duration = std::chrono::steady_clock::now() - start;
        funcionDurations[functionName] += std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    }
};


inline std::string getProfilingResults()
{
    std::stringstream ss;
    ss << "Function profiling results:" << std::endl;
    for (const auto& [name, duration] : FunctionProfiler::funcionDurations)
    {
        ss << "  " << name << ": " << duration.count() << " ns" << std::endl;
    }

    return ss.str();
}

}

#endif