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

#ifndef BEAST_ASIO_WAITABLE_EXECUTOR_H_INCLUDED
#define BEAST_ASIO_WAITABLE_EXECUTOR_H_INCLUDED

#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>
#include <vector>

namespace beast {
namespace asio {

namespace detail {

template <class Owner, class Handler>
class waitable_executor_wrapped_handler
{
private:
    static_assert (std::is_same <std::decay_t <Owner>, Owner>::value,
        "Owner cannot be a const or reference type");

    Handler handler_;
    std::reference_wrapper <Owner> owner_;
    bool cont_;

public:
    waitable_executor_wrapped_handler (Owner& owner,
        Handler&& handler, bool continuation = false)
        : handler_ (std::move(handler))
        , owner_ (owner)
    {
        using boost::asio::asio_handler_is_continuation;
        cont_ = continuation ? true :
            asio_handler_is_continuation(
                std::addressof(handler_));
        owner_.get().increment();
    }

    waitable_executor_wrapped_handler (Owner& owner,
        Handler const& handler, bool continuation = false)
        : handler_ (handler)
        , owner_ (owner)
    {
        using boost::asio::asio_handler_is_continuation;
        cont_ = continuation ? true :
            asio_handler_is_continuation(
                std::addressof(handler_));
        owner_.get().increment();
    }

    ~waitable_executor_wrapped_handler()
    {
        owner_.get().decrement();
    }

    waitable_executor_wrapped_handler (
            waitable_executor_wrapped_handler const& other)
        : handler_ (other.handler_)
        , owner_ (other.owner_)
        , cont_ (other.cont_)
    {
        owner_.get().increment();
    }

    waitable_executor_wrapped_handler (
            waitable_executor_wrapped_handler&& other)
        : handler_ (std::move(other.handler_))
        , owner_ (other.owner_)
        , cont_ (other.cont_)
    {
        owner_.get().increment();
    }

    waitable_executor_wrapped_handler& operator=(
        waitable_executor_wrapped_handler const&) = delete;

    template <class... Args>
    void
    operator()(Args&&... args)
    {
        handler_(std::forward<Args>(args)...);
    }

    template <class... Args>
    void
    operator()(Args&&... args) const
    {
        handler_(std::forward<Args>(args)...);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function& f,
        waitable_executor_wrapped_handler* h)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(f,
            std::addressof(h->handler_));
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function const& f,
        waitable_executor_wrapped_handler* h)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(f,
            std::addressof(h->handler_));
    }

    friend
    void*
    asio_handler_allocate (std::size_t size,
        waitable_executor_wrapped_handler* h)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(h->handler_));
    }

    friend
    void
    asio_handler_deallocate (void* p, std::size_t size,
        waitable_executor_wrapped_handler* h)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(h->handler_));
    }

    friend
    bool
    asio_handler_is_continuation (
        waitable_executor_wrapped_handler* h)
    {
        return h->cont_;
    }
};

} // detail

//------------------------------------------------------------------------------

/** Executor which provides blocking until all handlers are called. */
class waitable_executor
{
private:
    template <class, class>
    friend class detail::waitable_executor_wrapped_handler;

    std::mutex mutex_;
    std::condition_variable cond_;
    std::size_t count_ = 0;
    std::vector<std::function<void(void)>> notify_;

public:
    /** Block until all handlers are called. */
    template <class = void>
    void
    wait();

    /** Blocks until all handlers are called or time elapses.
        @return `true` if all handlers are done or `false` if the time elapses.
    */
    template <class Rep, class Period>
    bool
    wait_for (std::chrono::duration<
        Rep, Period> const& elapsed_time);

    /** Blocks until all handlers are called or a time is reached.
        @return `true` if all handlers are done or `false` on timeout.
    */
    template <class Clock, class Duration>
    bool
    wait_until (std::chrono::time_point<
        Clock, Duration> const& timeout_time);

    /** Call a function asynchronously after all handlers are called.
        The function may be called on the callers thread.
    */
    template <class = void>
    void
    async_wait(std::function<void(void)> f);

    /** Create a new handler that dispatches the wrapped handler on the Context. */
    template <class Handler>
    detail::waitable_executor_wrapped_handler<waitable_executor,
        std::remove_reference_t<Handler>>
    wrap (Handler&& handler);

private:
    template <class = void>
    void
    increment();

    template <class = void>
    void
    decrement();
};

template <class>
void
waitable_executor::wait()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock,
        [this]() { return count_ == 0; });
}

template <class Rep, class Period>
bool
waitable_executor::wait_for (std::chrono::duration<
    Rep, Period> const& elapsed_time)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return cond_.wait_for(lock, elapsed_time,
        [this]() { return count_ == 0; }) ==
            std::cv_status::no_timeout;
}

template <class Clock, class Duration>
bool
waitable_executor::wait_until (std::chrono::time_point<
    Clock, Duration> const& timeout_time)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return cond_.wait_until(lock, timeout_time,
        [this]() { return count_ == 0; }) ==
            std::cv_status::no_timeout;
    return true;
}

template <class>
void
waitable_executor::async_wait(std::function<void(void)> f)
{
    bool busy;
    {
        std::lock_guard<std::mutex> _(mutex_);
        busy = count_ > 0;
        if (busy)
            notify_.emplace_back(std::move(f));
    }
    if (! busy)
        f();
}

template <class Handler>
detail::waitable_executor_wrapped_handler<waitable_executor,
    std::remove_reference_t<Handler>>
waitable_executor::wrap (Handler&& handler)
{
    return detail::waitable_executor_wrapped_handler<
        waitable_executor, std::remove_reference_t<Handler>>(
            *this, std::forward<Handler>(handler));
}

template <class>
void
waitable_executor::increment()
{
    std::lock_guard<std::mutex> _(mutex_);
    ++count_;
}

template <class>
void
waitable_executor::decrement()
{
    bool notify;
    std::vector<std::function<void(void)>> list;
    {
        std::lock_guard<std::mutex> _(mutex_);
        notify = --count_ == 0;
        if (notify)
            std::swap(list, notify_);
    }
    if (notify)
    {
        cond_.notify_all();
        for(auto& _ : list)
            _();
    }
}

} // asio
} // beast

#endif
