//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_ASIO_IO_LATENCY_PROBE_H_INCLUDED
#define BEAST_ASIO_IO_LATENCY_PROBE_H_INCLUDED

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/config.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace beast {

/** Measures handler latency on an io_service queue. */
template <class Clock>
class io_latency_probe
{
private:
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;

    std::recursive_mutex m_mutex;
    std::condition_variable_any m_cond;
    std::size_t m_count;
    duration const m_period;
    boost::asio::io_service& m_ios;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> m_timer;
    bool m_cancel;

public:
    io_latency_probe (duration const& period,
        boost::asio::io_service& ios)
        : m_count (1)
        , m_period (period)
        , m_ios (ios)
        , m_timer (m_ios)
        , m_cancel (false)
    {
    }

    ~io_latency_probe ()
    {
        std::unique_lock <decltype (m_mutex)> lock (m_mutex);
        cancel (lock, true);
    }

    /** Return the io_service associated with the latency probe. */
    /** @{ */
    boost::asio::io_service& get_io_service ()
    {
        return m_ios;
    }

    boost::asio::io_service const& get_io_service () const
    {
        return m_ios;
    }
    /** @} */

    /** Cancel all pending i/o.
        Any handlers which have already been queued will still be called.
    */
    /** @{ */
    void cancel ()
    {
        std::unique_lock <decltype(m_mutex)> lock (m_mutex);
        cancel (lock, true);
    }

    void cancel_async ()
    {
        std::unique_lock <decltype(m_mutex)> lock (m_mutex);
        cancel (lock, false);
    }
    /** @} */

    /** Measure one sample of i/o latency.
        Handler will be called with this signature:
            void Handler (Duration d);
    */
    template <class Handler>
    void sample_one (Handler&& handler)
    {
        std::lock_guard lock (m_mutex);
        if (m_cancel)
            throw std::logic_error ("io_latency_probe is canceled");
        m_ios.post (sample_op <Handler> (
            std::forward <Handler> (handler),
                Clock::now(), false, this));
    }

    /** Initiate continuous i/o latency sampling.
        Handler will be called with this signature:
            void Handler (std::chrono::milliseconds);
    */
    template <class Handler>
    void sample (Handler&& handler)
    {
        std::lock_guard lock (m_mutex);
        if (m_cancel)
            throw std::logic_error ("io_latency_probe is canceled");
        m_ios.post (sample_op <Handler> (
            std::forward <Handler> (handler),
                Clock::now(), true, this));
    }

private:
    void cancel (std::unique_lock <decltype (m_mutex)>& lock,
        bool wait)
    {
        if (! m_cancel)
        {
            --m_count;
            m_cancel = true;
        }

        if (wait)
#ifdef BOOST_NO_CXX11_LAMBDAS
            while (m_count != 0)
                m_cond.wait (lock);
#else
            m_cond.wait (lock, [this] {
                return this->m_count == 0; });
#endif
    }

    void addref ()
    {
        std::lock_guard lock (m_mutex);
        ++m_count;
    }

    void release ()
    {
        std::lock_guard lock (m_mutex);
        if (--m_count == 0)
            m_cond.notify_all ();
    }

    template <class Handler>
    struct sample_op
    {
        Handler m_handler;
        time_point m_start;
        bool m_repeat;
        io_latency_probe* m_probe;

        sample_op (Handler const& handler, time_point const& start,
            bool repeat, io_latency_probe* probe)
            : m_handler (handler)
            , m_start (start)
            , m_repeat (repeat)
            , m_probe (probe)
        {
            assert(m_probe);
            m_probe->addref();
        }

        sample_op (sample_op&& from) noexcept
            : m_handler (std::move(from.m_handler))
            , m_start (from.m_start)
            , m_repeat (from.m_repeat)
            , m_probe (from.m_probe)
        {
            assert(m_probe);
            from.m_probe = nullptr;
        }

        sample_op (sample_op const&) = delete;
        sample_op operator= (sample_op const&) = delete;
        sample_op& operator= (sample_op&&) = delete;

        ~sample_op ()
        {
            if(m_probe)
                m_probe->release();
        }

        void operator() () const
        {
            if (!m_probe)
                return;
            typename Clock::time_point const now (Clock::now());
            typename Clock::duration const elapsed (now - m_start);

            m_handler (elapsed);

            {
                std::lock_guard lock (m_probe->m_mutex);
                if (m_probe->m_cancel)
                    return;
            }

            if (m_repeat)
            {
                // Calculate when we want to sample again, and
                // adjust for the expected latency.
                //
                typename Clock::time_point const when (
                    now + m_probe->m_period - 2 * elapsed);

                if (when <= now)
                {
                    // The latency is too high to maintain the desired
                    // period so don't bother with a timer.
                    //
                    m_probe->m_ios.post (sample_op <Handler> (
                        m_handler, now, m_repeat, m_probe));
                }
                else
                {
                    m_probe->m_timer.expires_from_now(when - now);
                    m_probe->m_timer.async_wait (sample_op <Handler> (
                        m_handler, now, m_repeat, m_probe));
                }
            }
        }

        void operator () (boost::system::error_code const& ec)
        {
            if (!m_probe)
                return;
            typename Clock::time_point const now (Clock::now());
            m_probe->m_ios.post (sample_op <Handler> (
                m_handler, now, m_repeat, m_probe));
        }
    };
};

}

#endif
