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

#include "../counted_bind.h"

namespace ripple {

using namespace beast;

class TrackedHandler
{
public:
    explicit TrackedHandler (Journal journal)
        : m_journal (journal)
    {
        m_journal.info << "Constructor";
    }

    TrackedHandler (TrackedHandler const& h)
        : m_journal (h.m_journal)
    {
        m_journal.info << "Copy Constructor";
    }

    TrackedHandler (TrackedHandler&& h)
        : m_journal (std::move (h.m_journal))
    {
        m_journal.info << "Move Constructor";
    }

    // VFALCO NOTE What the heck does this do?
    TrackedHandler (TrackedHandler const&& h)
        : m_journal (h.m_journal)
    {
        m_journal.info << "Move Constructor (const)";
    }

    ~TrackedHandler ()
    {
        m_journal.info << "Destructor";
    }

    void operator () () const
    {
        m_journal.info << "Function call";
    }

private:
    Journal m_journal;
};

//------------------------------------------------------------------------------

template <class Handler>
struct HandlerWrapper
{
#if 1
    // Universal constructor
    template <class H>
    HandlerWrapper (H&& h, std::nullptr_t) // dummy arg
        : m_handler (std::forward <H> (h))
    {
    }

#else
    // move construct
    HandlerWrapper (Handler&& h, nullptr_t)
        : m_handler (std::move (h))
    {
    }

    // copy construct
    HandlerWrapper (Handler const& h, nullptr_t)
        : m_handler (h)
    {
    }
#endif

#if 0
    template <typename ...Args>
    void operator () (Args&& ...args)
    {
        m_handler (std::forward <Args> (args)...);
    }

#else
    void operator() () const
    {
        m_handler ();
    }

#endif

    Handler const m_handler;
};

template <class Handler>
HandlerWrapper <
    typename std::remove_reference <Handler>::type> make_handler (Handler&& h)
{
    typedef typename std::remove_reference <Handler>::type handler_type;
    return HandlerWrapper <handler_type> (
        std::forward <handler_type> (h), nullptr);
}

//------------------------------------------------------------------------------

class CountedBindTests : public UnitTest
{
public:
    void runTest ()
    {
        beginTestCase ("Move");

        Journal const j (journal());

        {
            j.info << "w1";
            TrackedHandler h (j);
            HandlerWrapper <TrackedHandler> w1 (std::move (h), nullptr);
            w1 ();
        }

        {
            j.info << "w2";
            HandlerWrapper <TrackedHandler> w2 ((TrackedHandler (j)), nullptr);
            w2 ();
        }

        {
            j.info << "w3";
            TrackedHandler const h (j);
            HandlerWrapper <TrackedHandler> w3 (h, nullptr);
            w3 ();
        }

        {
            j.info << "w4";
            auto w4 (make_handler (TrackedHandler (j)));
            w4 ();
        }

        {
            j.info << "w5";
            TrackedHandler const h (j);
            auto w5 (make_handler (h));
            w5 ();
        }

        pass ();
    }

    CountedBindTests () : UnitTest ("counted_bind", "ripple", runManual)
    {
    }
};

static CountedBindTests countedBindTests;

}
