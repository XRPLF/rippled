#include <xrpl/core/LoadEvent.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/LoadMonitor.h>
#include <xrpl/basics/TraceLog.h>

#include <chrono>
#include <string>
#include <utility>

namespace xrpl {

LoadEvent::LoadEvent(LoadMonitor& monitor, std::string name, bool shouldStart)
    : monitor_(monitor)
    , running_(shouldStart)
    , name_(std::move(name))
    , mark_{std::chrono::steady_clock::now()}
    , timeWaiting_{}
    , timeRunning_{}
{
}

LoadEvent::~LoadEvent()
{
    TRACE_FUNC();
    if (running_)
        stop();
}

std::string const&
LoadEvent::name() const
{
    TRACE_FUNC();
    return name_;
}

std::chrono::steady_clock::duration
LoadEvent::waitTime() const
{
    TRACE_FUNC();
    return timeWaiting_;
}

std::chrono::steady_clock::duration
LoadEvent::runTime() const
{
    TRACE_FUNC();
    return timeRunning_;
}

void
LoadEvent::setName(std::string const& name)
{
    TRACE_FUNC();
    name_ = name;
}

void
LoadEvent::start()
{
    TRACE_FUNC();
    auto const now = std::chrono::steady_clock::now();

    // If we had already called start, this call will
    // replace the previous one. Any time accumulated will
    // be counted as "waiting".
    timeWaiting_ += now - mark_;
    mark_ = now;
    running_ = true;
}

void
LoadEvent::stop()
{
    TRACE_FUNC();
    XRPL_ASSERT(running_, "xrpl::LoadEvent::stop : is running");

    auto const now = std::chrono::steady_clock::now();

    timeRunning_ += now - mark_;
    mark_ = now;
    running_ = false;

    monitor_.addLoadSample(*this);
}

}  // namespace xrpl
