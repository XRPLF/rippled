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

#ifndef BEAST_ASIO_ENABLE_WAIT_FOR_ASYNC_H_INCLUDED
#define BEAST_ASIO_ENABLE_WAIT_FOR_ASYNC_H_INCLUDED

#include <beast/asio/wrap_handler.h>

#include <beast/utility/is_call_possible.h>

#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace beast {
namespace asio {

namespace detail {

template <class Owner, class Handler>
class ref_counted_wrapped_handler
{
private:
    static_assert (std::is_same <std::decay_t <Owner>, Owner>::value,
        "Owner cannot be a const or reference type");

    Handler m_handler;
    std::reference_wrapper <Owner> m_owner;
    bool m_continuation;

public:
    ref_counted_wrapped_handler (Owner& owner,
        Handler&& handler, bool continuation)
        : m_handler (std::move (handler))
        , m_owner (owner)
        , m_continuation (continuation ? true :
            boost_asio_handler_cont_helpers::is_continuation (m_handler))
    {
        m_owner.get().increment();
    }

    ref_counted_wrapped_handler (Owner& owner,
        Handler const& handler, bool continuation)
        : m_handler (handler)
        , m_owner (owner)
        , m_continuation (continuation ? true :
            boost_asio_handler_cont_helpers::is_continuation (m_handler))
    {
        m_owner.get().increment();
    }

    ~ref_counted_wrapped_handler ()
    {
        m_owner.get().decrement();
    }

    ref_counted_wrapped_handler (ref_counted_wrapped_handler const& other)
        : m_handler (other.m_handler)
        , m_owner (other.m_owner)
        , m_continuation (other.m_continuation)
    {
        m_owner.get().increment();
    }

    ref_counted_wrapped_handler (ref_counted_wrapped_handler&& other)
        : m_handler (std::move (other.m_handler))
        , m_owner (other.m_owner)
        , m_continuation (other.m_continuation)
    {
        m_owner.get().increment();
    }

    ref_counted_wrapped_handler& operator= (
        ref_counted_wrapped_handler const&) = delete;

    template <class... Args>
    void
    operator() (Args&&... args)
    {
        m_handler (std::forward <Args> (args)...);
    }

    template <class... Args>
    void
    operator() (Args&&... args) const
    {
        m_handler (std::forward <Args> (args)...);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function& f,
        ref_counted_wrapped_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke (f, h->m_handler);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function const& f,
        ref_counted_wrapped_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke (f, h->m_handler);
    }

    friend
    void*
    asio_handler_allocate (std::size_t size,
        ref_counted_wrapped_handler* h)
    {
        return boost_asio_handler_alloc_helpers::
            allocate (size, h->m_handler);
    }

    friend
    void
    asio_handler_deallocate (void* p, std::size_t size,
        ref_counted_wrapped_handler* h)
    {
        boost_asio_handler_alloc_helpers::
            deallocate (p, size, h->m_handler);
    }

    friend
    bool
    asio_handler_is_continuation (ref_counted_wrapped_handler* h)
    {
        return h->m_continuation;
    }
};

}

//------------------------------------------------------------------------------

/** Facilitates blocking until no completion handlers are remaining.
    If Derived has this member function:

    @code
        void on_wait_for_async (void)
    @endcode

    Then it will be called every time the number of pending completion
    handlers transitions to zero from a non-zero value. The call is made
    while holding the internal mutex.
*/
template <class Derived>
class enable_wait_for_async
{
private:
    BEAST_DEFINE_IS_CALL_POSSIBLE(
        has_on_wait_for_async,on_wait_for_async);

    void increment()
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        ++m_count;
    }

    void notify (std::true_type)
    {
        static_cast <Derived*> (this)->on_wait_for_async();
    }

    void notify (std::false_type)
    {
    }

    void decrement()
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        --m_count;
        if (m_count == 0)
        {
            m_cond.notify_all();
            notify (std::integral_constant <bool,
                has_on_wait_for_async<Derived, void(void)>::value>());
        }
    }

    template <class Owner, class Handler>
    friend class detail::ref_counted_wrapped_handler;

    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::size_t m_count;

public:
    /** Blocks if there are any pending completion handlers. */
    void
    wait_for_async()
    {
        std::unique_lock <decltype (m_mutex)> lock (m_mutex);
        while (m_count != 0)
            m_cond.wait (lock);
    }

protected:
    enable_wait_for_async()
        : m_count (0)
    {
    }

    ~enable_wait_for_async()
    {
        assert (m_count == 0);
    }

    /** Wraps the specified handler so it can be counted. */
    /** @{ */
    template <class Handler>
    detail::ref_counted_wrapped_handler <
        enable_wait_for_async,
        std::remove_reference_t <Handler>
    >
    wrap_with_counter (Handler&& handler, bool continuation = false)
    {
        return detail::ref_counted_wrapped_handler <enable_wait_for_async,
            std::remove_reference_t <Handler>> (*this,
                std::forward <Handler> (handler), continuation);
    }

    template <class Handler>
    detail::ref_counted_wrapped_handler <
        enable_wait_for_async,
        std::remove_reference_t <Handler>
    >
    wrap_with_counter (continuation_t, Handler&& handler)
    {
        return detail::ref_counted_wrapped_handler <enable_wait_for_async,
            std::remove_reference_t <Handler>> (*this,
                std::forward <Handler> (handler), true);
    }
    /** @} */
};

}
}

#endif
