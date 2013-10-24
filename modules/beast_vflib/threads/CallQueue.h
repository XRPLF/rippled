//------------------------------------------------------------------------------
/*
 This file is part of Beast: https://github.com/vinniefalco/Beast
 Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>
 Copyright Patrick Dehne <patrick@mysonicweb.de> (www.sonicweb-radio.de)
 
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

#ifndef BEAST_VFLIB_CALLQUEUE_H_INCLUDED
#define BEAST_VFLIB_CALLQUEUE_H_INCLUDED

#include "beast/Threads.h"
#include "../functor/BindHelper.h"

namespace beast {

template <class Allocator = std::allocator <char> >
class CallQueueType
: public ServiceQueueType <Allocator>
{
public:
    explicit CallQueueType (const String& name,
                            int expectedConcurrency = 1,
                            Allocator alloc = Allocator())
    : ServiceQueueType<Allocator>(expectedConcurrency, alloc)
    , m_name (name)
    , queue(*this)
    , call(*this)
    {
    }
    
    ~CallQueueType ()
    {
        // Someone forget to close the queue.
        bassert (m_closed.isSignaled ());
        
        // Can't destroy queue with unprocessed calls.
        bassert (ServiceQueueBase::empty ());
    }
    
    void enqueue (ServiceQueueBase::Item* item)
    {
        // If this goes off someone added calls
        // after the queue has been closed.
        bassert (!m_closed.isSignaled ());
        
        ServiceQueueType <Allocator>::enqueue (item);
    }
    
    /** Close the queue.
     
     Functors may not be added after this routine is called. This is used for
     diagnostics, to track down spurious calls during application shutdown
     or exit. Derived classes may call this if the appropriate time is known.
     
     The queue is synchronized after it is closed.
     Can still have pending calls, just can't put new ones in.
     */
    virtual void close ()
    {
        m_closed.signal ();
        
        ServiceQueueType <Allocator>::stop ();
    }
    
    struct BindHelperPost
    {
        CallQueueType<Allocator>& m_queue;
        explicit BindHelperPost (CallQueueType<Allocator>& queue)
        : m_queue (queue)
        { }
        template <typename F>
        void operator() (F const& f) const
        { m_queue.post ( F (f) ); }
    };
    
    struct BindHelperDispatch
    {
        CallQueueType<Allocator>& m_queue;
        explicit BindHelperDispatch (CallQueueType<Allocator>& queue)
        : m_queue (queue)
        { }
        template <typename F>
        void operator() (F const& f) const
        { m_queue.dispatch ( F (f) ); }
    };
    
    BindHelper <BindHelperPost> const queue;
    BindHelper <BindHelperDispatch> const call;
    
private:
    String m_name;
    AtomicFlag m_closed;
};

typedef CallQueueType <std::allocator <char> > CallQueue;

namespace detail
{

//------------------------------------------------------------------------------

class CallQueueTests
: public UnitTest
{
public:
    struct CallTracker
    {
        CallQueueTests *unitTest;
        int c0, c1, c2, c3, c4, c5, c6, c7, c8;
        int q0, q1, q2, q3, q4, q5, q6, q7, q8;
        
        CallTracker(CallQueueTests *parent)
        : unitTest(parent)
        , c0(0), c1(0), c2(0), c3(0), c4(0), c5(0), c6(0), c7(0), c8(0)
        , q0(0), q1(0), q2(0), q3(0), q4(0), q5(0), q6(0), q7(0), q8(0)
        {
        }
        
        void doQ0() { q0++; }
        
        void doQ1(const String& p1)
        {
            unitTest->expect(p1 == "p1");
            q1++;
        }
        
        void doQ2(const String& p1, const String& p2)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            q2++;
        }
        
        void doQ3(const String& p1, const String& p2, const String& p3)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            q3++;
        }
        
        void doQ4(const String& p1, const String& p2, const String& p3, const String& p4)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            q4++;
        }
        
        void doQ5(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            q5++;
        }
        
        void doQ6(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5, const String& p6)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            unitTest->expect(p6 == "p6");
            q6++;
        }
        
        void doQ7(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5, const String& p6, const String& p7)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            unitTest->expect(p6 == "p6");
            unitTest->expect(p7 == "p7");
            q7++;
        }
        
        void doQ8(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5, const String& p6, const String& p7, const String& p8)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            unitTest->expect(p6 == "p6");
            unitTest->expect(p7 == "p7");
            unitTest->expect(p8 == "p8");
            q8++;
        }
        
        void doC0() { c0++; }
        
        void doC1(const String& p1)
        {
            unitTest->expect(p1 == "p1");
            c1++;
        }
        
        void doC2(const String& p1, const String& p2)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            c2++;
        }
        
        void doC3(const String& p1, const String& p2, const String& p3)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            c3++;
        }
        
        void doC4(const String& p1, const String& p2, const String& p3, const String& p4)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            c4++;
        }
        
        void doC5(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            c5++;
        }
        
        void doC6(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5, const String& p6)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            unitTest->expect(p6 == "p6");
            c6++;
        }
        
        void doC7(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5, const String& p6, const String& p7)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            unitTest->expect(p6 == "p6");
            unitTest->expect(p7 == "p7");
            c7++;
        }
        
        void doC8(const String& p1, const String& p2, const String& p3, const String& p4, const String& p5, const String& p6, const String& p7, const String& p8)
        {
            unitTest->expect(p1 == "p1");
            unitTest->expect(p2 == "p2");
            unitTest->expect(p3 == "p3");
            unitTest->expect(p4 == "p4");
            unitTest->expect(p5 == "p5");
            unitTest->expect(p6 == "p6");
            unitTest->expect(p7 == "p7");
            unitTest->expect(p8 == "p8");
            c8++;
        }
    };
    
    CallTracker m_callTracker;
    
    void testArities ()
    {
        beginTestCase("Arities");
        
        int calls = 0;
        
#if BEAST_VARIADIC_MAX >= 2
        m_queue.queue(&CallTracker::doQ0, &m_callTracker); calls++;
        m_queue.queue(&CallTracker::doC0, &m_callTracker); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 3
        m_queue.queue(&CallTracker::doQ1, &m_callTracker, "p1"); calls++;
        m_queue.queue(&CallTracker::doC1, &m_callTracker, "p1"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 4
        m_queue.queue(&CallTracker::doQ2, &m_callTracker, "p1", "p2"); calls++;
        m_queue.queue(&CallTracker::doC2, &m_callTracker, "p1", "p2"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 5
        m_queue.queue(&CallTracker::doQ3, &m_callTracker, "p1", "p2", "p3"); calls++;
        m_queue.queue(&CallTracker::doC3, &m_callTracker, "p1", "p2", "p3"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 6
        m_queue.queue(&CallTracker::doQ4, &m_callTracker, "p1", "p2", "p3", "p4"); calls++;
        m_queue.queue(&CallTracker::doC4, &m_callTracker, "p1", "p2", "p3", "p4"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 7
        m_queue.queue(&CallTracker::doQ5, &m_callTracker, "p1", "p2", "p3", "p4", "p5"); calls++;
        m_queue.queue(&CallTracker::doC5, &m_callTracker, "p1", "p2", "p3", "p4", "p5"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 8
        m_queue.queue(&CallTracker::doQ6, &m_callTracker, "p1", "p2", "p3", "p4", "p5", "p6"); calls++;
        m_queue.queue(&CallTracker::doC6, &m_callTracker, "p1", "p2", "p3", "p4", "p5", "p6"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 9
        m_queue.queue(&CallTracker::doQ7, &m_callTracker, "p1", "p2", "p3", "p4", "p5", "p6", "p7"); calls++;
        m_queue.queue(&CallTracker::doC7, &m_callTracker, "p1", "p2", "p3", "p4", "p5", "p6", "p7"); calls++;
#endif

#if BEAST_VARIADIC_MAX >= 10
        m_queue.queue(&CallTracker::doQ8, &m_callTracker, "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8"); calls++;
        m_queue.queue(&CallTracker::doC8, &m_callTracker, "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8"); calls++;
#endif

        std::size_t performedCalls = m_queue.poll();
        
        m_queue.close();
        
        expect (performedCalls == calls);
        
        expect (m_callTracker.c0 == 1);
        expect (m_callTracker.c1 == 1);
        expect (m_callTracker.c2 == 1);
        expect (m_callTracker.c3 == 1);
        expect (m_callTracker.c4 == 1);
        expect (m_callTracker.c5 == 1);
        
        expect (m_callTracker.q0 == 1);
        expect (m_callTracker.q1 == 1);
        expect (m_callTracker.q2 == 1);
        expect (m_callTracker.q3 == 1);
        expect (m_callTracker.q4 == 1);
        expect (m_callTracker.q5 == 1);
    }
    
    void runTest()
    {
        testArities();
    }
    
    CallQueueTests ()
    : UnitTest ("CallQueue", "beast")
    , m_queue("CallQueue Test Queue")
    , m_callTracker(this)
    {
    }
    
    CallQueue m_queue;
};

}

}

#endif
