#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace beast {

/** Measures handler latency on an io_context queue. */
template <class Clock>
class IOLatencyProbe
{
private:
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;

    std::recursive_mutex mutex_;
    std::condition_variable_any cond_;
    std::size_t count_{1};
    duration const period_;
    boost::asio::io_context& ios_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    bool cancel_{false};

public:
    IOLatencyProbe(duration const& period, boost::asio::io_context& ios)
        : period_(period), ios_(ios), timer_(ios_)
    {
    }

    ~IOLatencyProbe()
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cancel(lock, true);
    }

    /** Return the io_context associated with the latency probe. */
    /** @{ */
    boost::asio::io_context&
    getIoContext()
    {
        return ios_;
    }

    [[nodiscard]] boost::asio::io_context const&
    getIoContext() const
    {
        return ios_;
    }
    /** @} */

    /** Cancel all pending i/o.
        Any handlers which have already been queued will still be called.
    */
    /** @{ */
    void
    cancel()
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cancel(lock, true);
    }

    void
    cancelAsync()
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cancel(lock, false);
    }
    /** @} */

    /** Measure one sample of i/o latency.
        Handler will be called with this signature:
            void Handler (Duration d);
    */
    template <class Handler>
    void
    sampleOne(Handler&& handler)
    {
        std::scoped_lock const lock(mutex_);
        if (cancel_)
            throw std::logic_error("IOLatencyProbe is canceled");
        boost::asio::post(
            ios_, SampleOp<Handler>(std::forward<Handler>(handler), Clock::now(), false, this));
    }

    /** Initiate continuous i/o latency sampling.
        Handler will be called with this signature:
            void Handler (std::chrono::milliseconds);
    */
    template <class Handler>
    void
    sample(Handler&& handler)
    {
        std::scoped_lock const lock(mutex_);
        if (cancel_)
            throw std::logic_error("IOLatencyProbe is canceled");
        boost::asio::post(
            ios_, SampleOp<Handler>(std::forward<Handler>(handler), Clock::now(), true, this));
    }

private:
    void
    cancel(std::unique_lock<decltype(mutex_)>& lock, bool wait)
    {
        if (!cancel_)
        {
            --count_;
            cancel_ = true;
        }

        if (wait)
            cond_.wait(lock, [this] { return this->count_ == 0; });
    }

    void
    addref()
    {
        std::scoped_lock const lock(mutex_);
        ++count_;
    }

    void
    release()
    {
        std::scoped_lock const lock(mutex_);
        if (--count_ == 0)
            cond_.notify_all();
    }

    template <class Handler>
    struct SampleOp
    {
        Handler handler;
        time_point start;
        bool repeat;
        IOLatencyProbe* probe;

        SampleOp(
            Handler const& handler,
            time_point const& start,
            bool repeat,
            IOLatencyProbe* probe)
            : handler(handler), start(start), repeat(repeat), probe(probe)
        {
            XRPL_ASSERT(
                probe,
                "beast::IOLatencyProbe::SampleOp::SampleOp : non-null "
                "probe input");
            probe->addref();
        }

        SampleOp(SampleOp&& from) noexcept
            : handler(std::move(from.handler))
            , start(from.start)
            , repeat(from.repeat)
            , probe(from.probe)
        {
            XRPL_ASSERT(
                probe,
                "beast::IOLatencyProbe::SampleOp::SampleOp(SampleOp&&) : "
                "non-null probe input");
            from.probe = nullptr;
        }

        SampleOp(SampleOp const&) = delete;
        SampleOp
        operator=(SampleOp const&) = delete;
        SampleOp&
        operator=(SampleOp&&) = delete;

        ~SampleOp()
        {
            if (probe)
                probe->release();
        }

        void
        operator()() const
        {
            if (probe == nullptr)
                return;
            typename Clock::time_point const now(Clock::now());
            typename Clock::duration const elapsed(now - start);

            handler(elapsed);

            {
                std::scoped_lock const lock(probe->mutex_);
                if (probe->cancel_)
                    return;
            }

            if (repeat)
            {
                // Calculate when we want to sample again, and
                // adjust for the expected latency.
                //
                typename Clock::time_point const when(now + probe->period_ - (2 * elapsed));

                if (when <= now)
                {
                    // The latency is too high to maintain the desired
                    // period so don't bother with a timer.
                    //
                    boost::asio::post(probe->ios_, SampleOp<Handler>(handler, now, repeat, probe));
                }
                else
                {
                    probe->timer_.expires_after(when - now);
                    probe->timer_.async_wait(SampleOp<Handler>(handler, now, repeat, probe));
                }
            }
        }

        void
        operator()(boost::system::error_code const& ec)
        {
            if (probe == nullptr)
                return;
            typename Clock::time_point const now(Clock::now());
            boost::asio::post(probe->ios_, SampleOp<Handler>(handler, now, repeat, probe));
        }
    };
};

}  // namespace beast
