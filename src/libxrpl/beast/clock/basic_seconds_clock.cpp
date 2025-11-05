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
class seconds_clock_thread
{
    using Clock = basic_seconds_clock::Clock;

    bool stop_;
    std::mutex mut_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<Clock::time_point::rep> tp_;

public:
    ~seconds_clock_thread();
    seconds_clock_thread();

    Clock::time_point
    now();

private:
    void
    run();
};

static_assert(std::atomic<std::chrono::steady_clock::rep>::is_always_lock_free);

seconds_clock_thread::~seconds_clock_thread()
{
    XRPL_ASSERT(
        thread_.joinable(),
        "beast::seconds_clock_thread::~seconds_clock_thread : thread joinable");
    {
        std::lock_guard lock(mut_);
        stop_ = true;
    }  // publish stop_ asap so if waiting thread times-out, it will see it
    cv_.notify_one();
    thread_.join();
}

seconds_clock_thread::seconds_clock_thread()
    : stop_{false}, tp_{Clock::now().time_since_epoch().count()}
{
    thread_ = std::thread(&seconds_clock_thread::run, this);
}

seconds_clock_thread::Clock::time_point
seconds_clock_thread::now()
{
    return Clock::time_point{Clock::duration{tp_.load()}};
}

void
seconds_clock_thread::run()
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

basic_seconds_clock::time_point
basic_seconds_clock::now()
{
    static seconds_clock_thread clk;
    return clk.now();
}

}  // namespace beast
