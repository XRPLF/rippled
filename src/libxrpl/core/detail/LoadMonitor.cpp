#include <xrpl/core/LoadMonitor.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/LoadEvent.h>

#include <chrono>
#include <mutex>

namespace xrpl {

/*

TODO
----

- Use Journal for logging

*/

//------------------------------------------------------------------------------

LoadMonitor::Stats::Stats() : latencyAvg(0), latencyPeak(0)
{
}

//------------------------------------------------------------------------------

LoadMonitor::LoadMonitor(beast::Journal j)
    : latencyMSAvg_(0)
    , latencyMSPeak_(0)
    , targetLatencyAvg_(0)
    , targetLatencyPk_(0)
    , lastUpdate_(UptimeClock::now())
    , j_(j)
{
}

// VFALCO NOTE WHY do we need "the mutex?" This dependence on
//         a hidden global, especially a synchronization primitive,
//         is a flawed design.
//         It's not clear exactly which data needs to be protected.
//
// call with the mutex
void
LoadMonitor::update()
{
    using namespace std::chrono_literals;
    auto now = UptimeClock::now();
    if (now == lastUpdate_)  // current
        return;

    // VFALCO TODO Why 8?
    if ((now < lastUpdate_) || (now > (lastUpdate_ + 8s)))
    {
        // way out of date
        counts_ = 0;
        latencyEvents_ = 0;
        latencyMSAvg_ = 0ms;
        latencyMSPeak_ = 0ms;
        lastUpdate_ = now;
        return;
    }

    // do exponential decay
    /*
        David:

        "Imagine if you add 10 to something every second. And you
         also reduce it by 1/4 every second. It will "idle" at 40,
         corresponding to 10 counts per second."
    */
    do
    {
        lastUpdate_ += 1s;
        counts_ -= ((counts_ + 3) / 4);
        latencyEvents_ -= ((latencyEvents_ + 3) / 4);
        latencyMSAvg_ -= (latencyMSAvg_ / 4);
        latencyMSPeak_ -= (latencyMSPeak_ / 4);
    } while (lastUpdate_ < now);
}

void
LoadMonitor::addLoadSample(LoadEvent const& s)
{
    using namespace std::chrono;

    auto const total = s.runTime() + s.waitTime();
    // Don't include "jitter" as part of the latency
    auto const latency = total < 2ms ? 0ms : round<milliseconds>(total);

    if (latency > 500ms)
    {
        auto mj = (latency > 1s) ? j_.warn() : j_.info();
        JLOG(mj) << "Job: " << s.name() << " run: " << round<milliseconds>(s.runTime()).count()
                 << "ms"
                 << " wait: " << round<milliseconds>(s.waitTime()).count() << "ms";
    }

    addSamples(1, latency);
}

/* Add multiple samples
   @param count The number of samples to add
   @param latencyMS The total number of milliseconds
*/
void
LoadMonitor::addSamples(int count, std::chrono::milliseconds latency)
{
    std::scoped_lock const sl(mutex_);

    update();
    counts_ += count;
    latencyEvents_ += count;
    latencyMSAvg_ += latency;
    latencyMSPeak_ += latency;

    auto const latencyPeak = latencyEvents_ * latency * 4 / count;

    if (latencyMSPeak_ < latencyPeak)
        latencyMSPeak_ = latencyPeak;
}

void
LoadMonitor::setTargetLatency(std::chrono::milliseconds avg, std::chrono::milliseconds pk)
{
    targetLatencyAvg_ = avg;
    targetLatencyPk_ = pk;
}

bool
LoadMonitor::isOverTarget(std::chrono::milliseconds avg, std::chrono::milliseconds peak)
{
    using namespace std::chrono_literals;
    return (targetLatencyPk_ > 0ms && (peak > targetLatencyPk_)) ||
        (targetLatencyAvg_ > 0ms && (avg > targetLatencyAvg_));
}

bool
LoadMonitor::isOver()
{
    std::scoped_lock const sl(mutex_);

    update();

    if (latencyEvents_ == 0)
        return false;

    return isOverTarget(
        latencyMSAvg_ / (latencyEvents_ * 4), latencyMSPeak_ / (latencyEvents_ * 4));
}

LoadMonitor::Stats
LoadMonitor::getStats()
{
    using namespace std::chrono_literals;
    Stats stats;

    std::scoped_lock const sl(mutex_);

    update();

    stats.count = counts_ / 4;

    if (latencyEvents_ == 0)
    {
        stats.latencyAvg = 0ms;
        stats.latencyPeak = 0ms;
    }
    else
    {
        stats.latencyAvg = latencyMSAvg_ / (latencyEvents_ * 4);
        stats.latencyPeak = latencyMSPeak_ / (latencyEvents_ * 4);
    }

    stats.isOverloaded = isOverTarget(stats.latencyAvg, stats.latencyPeak);

    return stats;
}

}  // namespace xrpl
