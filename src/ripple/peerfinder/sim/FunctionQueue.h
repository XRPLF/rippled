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

#ifndef RIPPLE_PEERFINDER_SIM_FUNCTIONQUEUE_H_INCLUDED
#define RIPPLE_PEERFINDER_SIM_FUNCTIONQUEUE_H_INCLUDED

namespace ripple {
namespace PeerFinder {
namespace Sim {

/** Maintains a queue of functors that can be called later. */
class FunctionQueue
{
public:
    explicit FunctionQueue() = default;

private:
    class BasicWork
    {
    public:
        virtual ~BasicWork ()
            { }
        virtual void operator() () = 0;
    };

    template <typename Function>
    class Work : public BasicWork
    {
    public:
        explicit Work (Function f)
            : m_f (f)
            { }
        void operator() ()
            { (m_f)(); }
    private:
        Function m_f;
    };

    std::list <std::unique_ptr <BasicWork>> m_work;

public:
    /** Returns `true` if there is no remaining work */
    bool empty ()
        { return m_work.empty(); }

    /** Queue a function.
        Function must be callable with this signature:
            void (void)
    */
    template <typename Function>
    void post (Function f)
    {
        m_work.emplace_back (std::make_unique<Work <Function>>(f));
    }

    /** Run all pending functions.
        The functions will be invoked in the order they were queued.
    */
    void run ()
    {
        while (! m_work.empty ())
        {
            (*m_work.front())();
            m_work.pop_front();
        }
    }
};

}
}
}

#endif
