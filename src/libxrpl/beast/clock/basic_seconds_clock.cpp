#include <xrpl/beast/clock/basic_seconds_clock.h>

#include <xrpl/beast/utility/instrumentation.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace beast {

namespace {

// Updates the clock
class SecondsClockThread
{
    using Clock = BasicSecondsClock::Clock;

    bool stop_{false};
    std::mutex mut_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<Clock::time_point::rep> tp_;

public:
    ~SecondsClockThread();
    SecondsClockThread();

    Clock::time_point
    now();

private:
    void
    run();
};

static_assert(std::atomic<std::chrono::steady_clock::rep>::is_always_lock_free);

SecondsClockThread::~SecondsClockThread()
{
    XRPL_ASSERT(
        thread_.joinable(), "beast::SecondsClockThread::~SecondsClockThread : thread joinable");
    {
        std::scoped_lock const lock(mut_);
        stop_ = true;
    }  // publish stop_ asap so if waiting thread times-out, it will see it
    cv_.notify_one();
    thread_.join();
}

SecondsClockThread::SecondsClockThread() : tp_{Clock::now().time_since_epoch().count()}
{
    thread_ = std::thread(&SecondsClockThread::run, this);
}

SecondsClockThread::Clock::time_point
SecondsClockThread::now()
{
    return Clock::time_point{Clock::duration{tp_.load()}};
}

void
SecondsClockThread::run()
{
    std::unique_lock lock(mut_);
    while (true)
    {
        using namespace std::chrono;

        auto now = Clock::now();
        tp_ = now.time_since_epoch().count();
        auto const when = floor<seconds>(now) + 1s;
        if (cv_.wait_until(lock, when, [this] { return stop_; }))
            return;
    }
}

}  // unnamed namespace

BasicSecondsClock::time_point
BasicSecondsClock::now()
{
    static SecondsClockThread kCLK;
    return kCLK.now();
}

}  // namespace beast
