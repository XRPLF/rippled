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
class io_latency_probe
{
private:
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;

    std::recursive_mutex mutex_;
    std::condition_variable_any cond_;
    std::size_t count_;
    duration const period_;
    boost::asio::io_context& ios_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    bool cancel_;

public:
    io_latency_probe(duration const& period, boost::asio::io_context& ios)
        : count_(1), period_(period), ios_(ios), timer_(ios_), cancel_(false)
    {
    }

    ~io_latency_probe()
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cancel(lock, true);
    }

    /** Return the io_context associated with the latency probe. */
    /** @{ */
    boost::asio::io_context&
    get_io_context()
    {
        return ios_;
    }

    boost::asio::io_context const&
    get_io_context() const
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
    cancel_async()
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
    sample_one(Handler&& handler)
    {
        std::lock_guard lock(mutex_);
        if (cancel_)
            throw std::logic_error("io_latency_probe is canceled");
        boost::asio::post(
            ios_, sample_op<Handler>(std::forward<Handler>(handler), Clock::now(), false, this));
    }

    /** Initiate continuous i/o latency sampling.
        Handler will be called with this signature:
            void Handler (std::chrono::milliseconds);
    */
    template <class Handler>
    void
    sample(Handler&& handler)
    {
        std::lock_guard lock(mutex_);
        if (cancel_)
            throw std::logic_error("io_latency_probe is canceled");
        boost::asio::post(
            ios_, sample_op<Handler>(std::forward<Handler>(handler), Clock::now(), true, this));
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
        std::lock_guard lock(mutex_);
        ++count_;
    }

    void
    release()
    {
        std::lock_guard lock(mutex_);
        if (--count_ == 0)
            cond_.notify_all();
    }

    template <class Handler>
    struct sample_op
    {
        Handler handler_;
        time_point start_;
        bool repeat_;
        io_latency_probe* probe_;

        sample_op(
            Handler const& handler,
            time_point const& start,
            bool repeat,
            io_latency_probe* probe)
            : handler_(handler), start_(start), repeat_(repeat), probe_(probe)
        {
            XRPL_ASSERT(
                probe_,
                "beast::io_latency_probe::sample_op::sample_op : non-null "
                "probe input");
            probe_->addref();
        }

        sample_op(sample_op&& from) noexcept
            : handler_(std::move(from.handler_))
            , start_(from.start_)
            , repeat_(from.repeat_)
            , probe_(from.probe_)
        {
            XRPL_ASSERT(
                probe_,
                "beast::io_latency_probe::sample_op::sample_op(sample_op&&) : "
                "non-null probe input");
            from.probe_ = nullptr;
        }

        sample_op(sample_op const&) = delete;
        sample_op
        operator=(sample_op const&) = delete;
        sample_op&
        operator=(sample_op&&) = delete;

        ~sample_op()
        {
            if (probe_)
                probe_->release();
        }

        void
        operator()() const
        {
            if (!probe_)
                return;
            typename Clock::time_point const now(Clock::now());
            typename Clock::duration const elapsed(now - start_);

            handler_(elapsed);

            {
                std::lock_guard lock(probe_->mutex_);
                if (probe_->cancel_)
                    return;
            }

            if (repeat_)
            {
                // Calculate when we want to sample again, and
                // adjust for the expected latency.
                //
                typename Clock::time_point const when(now + probe_->period_ - 2 * elapsed);

                if (when <= now)
                {
                    // The latency is too high to maintain the desired
                    // period so don't bother with a timer.
                    //
                    boost::asio::post(
                        probe_->ios_, sample_op<Handler>(handler_, now, repeat_, probe_));
                }
                else
                {
                    probe_->timer_.expires_after(when - now);
                    probe_->timer_.async_wait(sample_op<Handler>(handler_, now, repeat_, probe_));
                }
            }
        }

        void
        operator()(boost::system::error_code const& ec)
        {
            if (!probe_)
                return;
            typename Clock::time_point const now(Clock::now());
            boost::asio::post(probe_->ios_, sample_op<Handler>(handler_, now, repeat_, probe_));
        }
    };
};

}  // namespace beast
