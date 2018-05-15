//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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
#include <ripple/beast/asio/io_latency_probe.h>
#include <ripple/beast/unit_test.h>
#include <beast/test/yield_to.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <chrono>

using namespace std::chrono_literals;

class io_latency_probe_test :
    public beast::unit_test::suite, public beast::test::enable_yield_to
{
    using MyTimer = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    struct test_sampler
    {
        beast::io_latency_probe <std::chrono::steady_clock> probe_;
        std::vector<std::chrono::steady_clock::duration> durations_;

        test_sampler (
            std::chrono::milliseconds interval,
            boost::asio::io_service& ios)
            : probe_ (interval, ios)
        {
        }

        void
        start()
        {
            probe_.sample (std::ref(*this));
        }

        void
        start_one()
        {
            probe_.sample_one (std::ref(*this));
        }

        void operator() (std::chrono::steady_clock::duration const& elapsed)
        {
            durations_.push_back(elapsed);
        }
    };

    void
    testSampleOne(boost::asio::yield_context& yield)
    {
        testcase << "sample one";
        boost::system::error_code ec;
        test_sampler io_probe {100ms, get_io_service()};
        io_probe.start_one();
        MyTimer timer {get_io_service(), 1s};
        timer.async_wait(yield[ec]);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(io_probe.durations_.size() == 1);
        io_probe.probe_.cancel_async();
    }

    void
    testSampleOngoing(boost::asio::yield_context& yield)
    {
        testcase << "sample ongoing";
        boost::system::error_code ec;
        test_sampler io_probe {99ms, get_io_service()};
        io_probe.start();
        MyTimer timer {get_io_service(), 1s};
        timer.async_wait(yield[ec]);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        auto probes_seen = io_probe.durations_.size();
        BEAST_EXPECTS(probes_seen >=9 && probes_seen <= 11,
            std::string("probe count is ") + std::to_string(probes_seen));
        io_probe.probe_.cancel_async();
        // wait again in order to flush the remaining
        // probes from the work queue
        timer.expires_from_now(1s);
        timer.async_wait(yield[ec]);
    }

    void
    testCanceled(boost::asio::yield_context& yield)
    {
        testcase << "canceled";
        test_sampler io_probe {100ms, get_io_service()};
        io_probe.probe_.cancel_async();
        except<std::logic_error>( [&io_probe](){ io_probe.start_one(); });
        except<std::logic_error>( [&io_probe](){ io_probe.start(); });
    }

public:
    void run () override
    {
        yield_to([&](boost::asio::yield_context& yield)
        {
            testSampleOne (yield);
            testSampleOngoing (yield);
            testCanceled (yield);
        });
    }
};

BEAST_DEFINE_TESTSUITE(io_latency_probe, asio, beast);
