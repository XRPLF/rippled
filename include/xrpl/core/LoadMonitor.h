#pragma once

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/LoadEvent.h>

#include <chrono>
#include <mutex>

namespace xrpl {

// Monitors load levels and response times

// VFALCO TODO Rename this. Having both LoadManager and LoadMonitor is
// confusing.
//
class LoadMonitor
{
public:
    explicit LoadMonitor(beast::Journal j);

    void
    addLoadSample(LoadEvent const& sample);

    void
    addSamples(int count, std::chrono::milliseconds latency);

    void
    setTargetLatency(std::chrono::milliseconds avg, std::chrono::milliseconds pk);

    bool
    isOverTarget(std::chrono::milliseconds avg, std::chrono::milliseconds peak);

    // VFALCO TODO make this return the values in a struct.
    struct Stats
    {
        Stats();

        std::uint64_t count{0};
        std::chrono::milliseconds latencyAvg;
        std::chrono::milliseconds latencyPeak;
        bool isOverloaded{false};
    };

    Stats
    getStats();

    bool
    isOver();

private:
    void
    update();

    std::mutex mutex_;

    std::uint64_t counts_{0};
    int latencyEvents_{0};
    std::chrono::milliseconds latencyMSAvg_;
    std::chrono::milliseconds latencyMSPeak_;
    std::chrono::milliseconds targetLatencyAvg_;
    std::chrono::milliseconds targetLatencyPk_;
    UptimeClock::time_point lastUpdate_;
    beast::Journal const j_;
};

}  // namespace xrpl
