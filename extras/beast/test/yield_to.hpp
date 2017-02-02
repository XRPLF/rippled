//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TEST_YIELD_TO_HPP
#define BEAST_TEST_YIELD_TO_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/optional.hpp>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace beast {
namespace test {

/** Mix-in to support tests using asio coroutines.

    Derive from this class and use yield_to to launch test
    functions inside coroutines. This is handy for testing
    asynchronous asio code.
*/
class enable_yield_to
{
protected:
    boost::asio::io_service ios_;

private:
    boost::optional<boost::asio::io_service::work> work_;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    bool running_ = false;

public:
    /// The type of yield context passed to functions.
    using yield_context =
        boost::asio::yield_context;

    enable_yield_to()
        : work_(ios_)
        , thread_([&]
            {
                ios_.run();
            }
        )
    {
    }

    ~enable_yield_to()
    {
        work_ = boost::none;
        thread_.join();
    }

    /// Return the `io_service` associated with the object
    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }

    /** Run a function in a coroutine.

        This call will block until the coroutine terminates.

        Function will be called with this signature:

        @code
            void f(args..., yield_context);
        @endcode

        @param f The Callable object to invoke.

        @param args Optional arguments forwarded to the callable object.
    */
#if GENERATING_DOCS
    template<class F, class... Args>
    void
    yield_to(F&& f, Args&&... args);
#else
    template<class F>
    void
    yield_to(F&& f);

    template<class Function, class Arg, class... Args>
    void
    yield_to(Function&& f, Arg&& arg, Args&&... args)
    {
        yield_to(std::bind(f,
            std::forward<Arg>(arg),
                std::forward<Args>(args)...,
                    std::placeholders::_1));
    }
#endif
};

template<class Function>
void
enable_yield_to::yield_to(Function&& f)
{
    {
        std::lock_guard<std::mutex> lock(m_);
        running_ = true;
    }
    boost::asio::spawn(ios_,
        [&](boost::asio::yield_context do_yield)
        {
            f(do_yield);
            std::lock_guard<std::mutex> lock(m_);
            running_ = false;
            cv_.notify_all();
        }
        , boost::coroutines::attributes(2 * 1024 * 1024));

    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [&]{ return ! running_; });
}

} // test
} // beast

#endif
