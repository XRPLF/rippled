//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_COUNTED_BIND_H_INCLUDED
#define RIPPLE_COUNTED_BIND_H_INCLUDED

#include <atomic>
#include <functional>
#include <utility>

namespace ripple {

namespace detail {

// Wrapper for managing the handler count
template <class Handler, class Counter>
class counted_bind_wrapper
{
public:
    template <class H>
    counted_bind_wrapper (H&& h, Counter& c)
        : m_handler (std::forward <H> (h))
        , m_counter (c)
    {
        ++m_counter;
    }

    counted_bind_wrapper (counted_bind_wrapper&& w)
        : m_handler (std::move (w.m_handler))
        , m_counter (w.m_counter)
    {
        ++m_counter;
    }

    counted_bind_wrapper (counted_bind_wrapper const& w)
        : m_handler (w.m_handler)
        , m_counter (w.m_counter)
    {
        ++m_counter;
    }

    ~counted_bind_wrapper ()
    {
        --m_counter;
    }

    //counted_bind_wrapper& operator= (counted_bind_wrapper const&) = delete;

#if 0
    // When variadic template arguments are supported
    template <typename ...Args>
    void operator () (Args&& ...args) const
    {
        m_handler (std::forward <Args> (args)...);
    }

#else
    void operator() ()
    {
        m_handler ();
    }

    void operator() () const
    {
        m_handler ();
    }

    template <class P1>
    void operator() (P1 const& p1)
    {
        m_handler (p1);
    }

    template <class P1>
    void operator() (P1 const& p1) const
    {
        m_handler (p1);
    }

    template <class P1, class P2>
    void operator() (P1 const& p1, P2 const& p2)
    {
        m_handler (p1, p2);
    }

    template <class P1, class P2>
    void operator() (P1 const& p1, P2 const& p2) const
    {
        m_handler (p1, p2);
    }

    template <class P1, class P2, class P3>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3)
    {
        m_handler (p1, p2, p3);
    }

    template <class P1, class P2, class P3>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3) const
    {
        m_handler (p1, p2, p3);
    }

    template <class P1, class P2, class P3, class P4>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4)
    {
        m_handler (p1, p2, p3, p4);
    }

    template <class P1, class P2, class P3, class P4>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4) const
    {
        m_handler (p1, p2, p3, p4);
    }

    template <class P1, class P2, class P3, class P4, class P5>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5)
    {
        m_handler (p1, p2, p3, p4, p5);
    }

    template <class P1, class P2, class P3, class P4, class P5>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5) const
    {
        m_handler (p1, p2, p3, p4, p5);
    }

    template <class P1, class P2, class P3, class P4, class P5, class P6>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5, P6 const& p6)
    {
        m_handler (p1, p2, p3, p4, p5, p6);
    }

    template <class P1, class P2, class P3, class P4, class P5, class P6>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5, P6 const& p6) const
    {
        m_handler (p1, p2, p3, p4, p5, p6);
    }

    template <class P1, class P2, class P3, class P4,
              class P5, class P6, class P7>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5, P6 const& p6, P7 const& p7)
    {
        m_handler (p1, p2, p3, p4, p5, p6, p7);
    }

    template <class P1, class P2, class P3, class P4,
              class P5, class P6, class P7>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5, P6 const& p6, P7 const& p7) const
    {
        m_handler (p1, p2, p3, p4, p5, p6, p7);
    }

    template <class P1, class P2, class P3, class P4,
              class P5, class P6, class P7, class P8>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5, P6 const& p6, P7 const& p7, P8 const& p8)
    {
        m_handler (p1, p2, p3, p4, p5, p6, p7, p8);
    }

    template <class P1, class P2, class P3, class P4,
              class P5, class P6, class P7, class P8>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4,
                     P5 const& p5, P6 const& p6, P7 const& p7, P8 const& p8) const
    {
        m_handler (p1, p2, p3, p4, p5, p6, p7, p8);
    }

#endif

private:
    Handler m_handler;
    Counter& m_counter;
};

}

//------------------------------------------------------------------------------

/** Provides a counted_bind replacement for bind which counts pending I/Os.
    Derive your class from this class and then call the bind member
    function instead.
*/
class enable_counted_bind
{
public:
    typedef std::size_t size_type;

private:
    typedef std::atomic <size_type> counter_type;

public:
    enable_counted_bind ()
        : m_count (0)
    {
    }

    /** Return the number of binds pending completion. */
    size_type bind_count () const
    {
        return m_count.load ();
    }

    /** Returns a wrapper that calls the handler and manages the counter. */
    template <class Handler>
    detail::counted_bind_wrapper <Handler, counter_type> wrap (Handler&& h)
    {
        return detail::counted_bind_wrapper <Handler, counter_type> (
            std::forward <Handler> (h), m_count);
    }

private:
    counter_type m_count;
};

}

#endif
