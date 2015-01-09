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

#ifndef BEAST_UNIT_TEST_THREAD_H_INCLUDED
#define BEAST_UNIT_TEST_THREAD_H_INCLUDED

#include <beast/utility/noexcept.h>
#include <beast/unit_test/suite.h>
#include <functional>
#include <thread>
#include <utility>

namespace beast {
namespace unit_test {

/** Replacement for std::thread that handles exceptions in unit tests. */
class thread
{
private:
    suite* s_ = nullptr;
    std::thread t_;

public:
    using id = std::thread::id;
    using native_handle_type = std::thread::native_handle_type;

    thread() = default;
    thread (thread const&) = delete;
    thread& operator= (thread const&) = delete;

    thread (thread&& other)
        : s_ (other.s_)
        , t_ (std::move(other.t_))
    {
    }

    thread& operator= (thread&& other)
    {
        s_ = other.s_;
        t_ = std::move(other.t_);
        return *this;
    }

    template <class F, class... Args>
    explicit
    thread (suite& s, F&& f, Args&&... args)
        : s_ (&s)
    {
        std::function<void(void)> b = 
            std::bind(std::forward<F>(f),
                std::forward<Args>(args)...);
        t_ = std::thread (&thread::run, this,
            std::move(b));
    }

    bool
    joinable() const
    {
        return t_.joinable();
    }

    std::thread::id
    get_id() const
    {
        return t_.get_id();
    }

    static
    unsigned
    hardware_concurrency() noexcept
    {
        return std::thread::hardware_concurrency();
    }

    void
    join()
    {
        t_.join();
        s_->propagate_abort();
    }

    void
    swap (thread& other)
    {
        std::swap(s_, other.s_);
        std::swap(t_, other.t_);
    }

private:
    void
    run (std::function <void(void)> f)
    {
        try
        {
            f();
        }
        catch (suite::abort_exception const&)
        {
        }
    }
};

} // unit_test
} // beast

#endif
