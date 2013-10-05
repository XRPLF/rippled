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

#ifndef RIPPLE_PEERFINDER_CHECKERADAPTER_H_INCLUDED
#define RIPPLE_PEERFINDER_CHECKERADAPTER_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Adapts a ServiceQueue to dispatch Checker handler completions.
    This lets the Logic have its Checker handler get dispatched
    on the ServiceQueue instead of an io_service thread. Otherwise,
    Logic would need a ServiceQueue to dispatch from its handler.
*/
class CheckerAdapter : public Checker
{
private:
    ServiceQueue& m_queue;
    ScopedPointer <Checker> m_checker;

    struct Handler
    {
        ServiceQueue* m_queue;
        AbstractHandler <void (Checker::Result)> m_handler;

        Handler (
            ServiceQueue& queue,
            AbstractHandler <void (Checker::Result)> handler)
            : m_queue (&queue)
            , m_handler (handler)
            { }

        void operator() (Checker::Result result)
        {
            m_queue->wrap (m_handler) (result);
        }
    };

public:
    explicit CheckerAdapter (ServiceQueue& queue)
        : m_queue (queue)
        , m_checker (Checker::New())
    {
    }

    ~CheckerAdapter ()
    {
        // Have to do this before other fields get destroyed
        m_checker = nullptr;
    }

    void cancel ()
    {
        m_checker->cancel();
    }

    void async_test (IPEndpoint const& endpoint,
        AbstractHandler <void (Checker::Result)> handler)
    {
        m_checker->async_test (endpoint, Handler (m_queue, handler));
    }
};

}
}

#endif
