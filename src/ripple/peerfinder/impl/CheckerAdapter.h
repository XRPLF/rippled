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

#include <memory>

namespace ripple {
namespace PeerFinder {

// Ensures that all Logic member function entry points are
// called while holding a lock on the recursive mutex.
//
typedef beast::ScopedWrapperContext <
    beast::RecursiveMutex, beast::RecursiveMutex::ScopedLockType> SerializedContext;

/** Adapts a ServiceQueue to dispatch Checker handler completions.
    This lets the Logic have its Checker handler get dispatched
    on the ServiceQueue instead of an io_service thread. Otherwise,
    Logic would need a ServiceQueue to dispatch from its handler.
*/
class CheckerAdapter : public Checker
{
private:
    SerializedContext& m_context;
    beast::ServiceQueue& m_queue;
    std::unique_ptr <Checker> m_checker;

    struct Handler
    {
        SerializedContext& m_context;
        beast::ServiceQueue& m_queue;
        beast::asio::shared_handler <void (Checker::Result)> m_handler;

        Handler (
            SerializedContext& context,
            beast::ServiceQueue& queue,
            beast::asio::shared_handler <void (Checker::Result)> const& handler)
            : m_context (context)
            , m_queue (queue)
            , m_handler (handler)
            { }

        void operator() (Checker::Result result)
        {
            // VFALCO TODO Fix this, it is surely wrong but
            //             this supposedly correct line doesn't compile
            //m_queue.wrap (m_context.wrap (m_handler)) (result);

            // WRONG
            m_queue.wrap (m_handler) (result);
        }
    };

public:
    CheckerAdapter (SerializedContext& context, beast::ServiceQueue& queue)
        : m_context (context)
        , m_queue (queue)
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

    void async_test (beast::IP::Endpoint const& endpoint,
        beast::asio::shared_handler <void (Checker::Result)> handler)
    {
        m_checker->async_test (endpoint, Handler (
            m_context, m_queue, handler));
    }
};

}
}

#endif
