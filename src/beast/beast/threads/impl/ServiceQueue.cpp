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

#include "../ServiceQueue.h"

#include "../../../modules/beast_core/beast_core.h" // for UnitTest

namespace beast {

class ServiceQueueBase::ScopedServiceThread : public List <ScopedServiceThread>::Node
{
public:
    explicit ScopedServiceThread (ServiceQueueBase* queue)
        : m_saved (ServiceQueueBase::s_service.get())
    {
        ServiceQueueBase::s_service.get() = queue;
    }

    ~ScopedServiceThread()
    {
        ServiceQueueBase::s_service.get() = m_saved;
    }

private:
    ServiceQueueBase* m_saved;
};

//------------------------------------------------------------------------------

ServiceQueueBase::ServiceQueueBase()
{
}

ServiceQueueBase::~ServiceQueueBase()
{
}

std::size_t ServiceQueueBase::poll ()
{
    CPUMeter::ScopedActiveTime interval (m_cpuMeter);

    std::size_t total (0);
    ScopedServiceThread thread (this);
    for (;;)
    {
        std::size_t const n (dequeue());
        if (! n)
            break;
        total += n;
    }
    return total;
}

std::size_t ServiceQueueBase::poll_one ()
{
    CPUMeter::ScopedActiveTime interval (m_cpuMeter);

    ScopedServiceThread thread (this);
    return dequeue();
}

std::size_t ServiceQueueBase::run ()
{
    std::size_t total (0);
    ScopedServiceThread thread (this);
    while (! stopped())
    {
        {
            CPUMeter::ScopedActiveTime interval (m_cpuMeter);
            total += poll ();
        }

        {
            CPUMeter::ScopedIdleTime interval (m_cpuMeter);
            wait ();
        }
    }
    return total;
}

std::size_t ServiceQueueBase::run_one ()
{
    std::size_t n;
    ScopedServiceThread (this);
    for (;;)
    {
        {
            CPUMeter::ScopedActiveTime interval (m_cpuMeter);
            n = poll_one();
            if (n != 0)
                break;
        }

        {
            CPUMeter::ScopedIdleTime interval (m_cpuMeter);
            wait();
        }
    }
    return n;
}

void ServiceQueueBase::stop ()
{
    SharedState::Access state (m_state);
    m_stopped.set (1);
    while (! state->waiting.empty ())
    {
        Waiter& waiting (state->waiting.front());
        state->waiting.pop_front ();
        waiting.signal ();
    }
}

void ServiceQueueBase::reset()
{
    // Must be stopped
    bassert (m_stopped.get () != 0);
    m_stopped.set (0);
}

// Block on the event if there are no items
// in the queue and we are not stopped.
//
void ServiceQueueBase::wait ()
{
    Waiter* waiter (nullptr);

    {
        SharedState::Access state (m_state);

        if (stopped ())
            return;

        if (! state->handlers.empty())
            return;

        if (state->unused.empty ())
        {
            waiter = new_waiter();
        }
        else
        {
            waiter = &state->unused.front ();
            state->unused.pop_front ();
        }

        state->waiting.push_front (*waiter);
    }

    waiter->wait();

    // Waiter got taken off the waiting list

    {
        SharedState::Access state (m_state);
        state->unused.push_front (*waiter);
    }
}

void ServiceQueueBase::enqueue (Item* item)
{
    Waiter* waiter (nullptr);

    {
        SharedState::Access state (m_state);
        state->handlers.push_back (*item);
        if (! state->waiting.empty ())
        {
            waiter = &state->waiting.front ();
            state->waiting.pop_front ();
        }
    }

    if (waiter != nullptr)
        waiter->signal();
}

bool ServiceQueueBase::empty()
{
    SharedState::Access state (m_state);
    return state->handlers.empty();
}

// A thread can only be blocked on one ServiceQueue so we store the pointer
// to which ServiceQueue it is blocked on to determine if the thread belongs
// to that queue.
//
ThreadLocalValue <ServiceQueueBase*> ServiceQueueBase::s_service;

//------------------------------------------------------------------------------

namespace detail {

//------------------------------------------------------------------------------

class ServiceQueueTimingTests
    : public UnitTest
{
public:
    class Stopwatch
    {
    public:
        Stopwatch () { start(); }
        void start () { m_startTime = Time::getHighResolutionTicks (); }
        double getElapsed ()
        {
            int64 const now = Time::getHighResolutionTicks();
            return Time::highResolutionTicksToSeconds (now - m_startTime);
        }
    private:
        int64 m_startTime;
    };

    static int const callsPerThread = 50000;

    //--------------------------------------------------------------------------

    template <typename ServiceType>
    struct Consumer : Thread
    {
        ServiceType& m_service;
        Random m_random;
        String m_string;

        Consumer (int id, int64 seedValue, ServiceType& service)
            : Thread ("C#" + String::fromNumber (id))
            , m_service (service)
            , m_random (seedValue)
            { startThread(); }

        ~Consumer ()
            { stopThread(); }

        static Consumer*& thread()
        {
            static ThreadLocalValue <Consumer*> local;
            return local.get();
        }

        static void stop_one ()
            { thread()->signalThreadShouldExit(); }

        static void handler ()
            { thread()->do_handler(); }

        void do_handler()
        {
            String const s (String::fromNumber (m_random.nextInt()));
            m_string += s;
            if (m_string.length() > 100)
                m_string = String::empty;
        }

        void run ()
        {
            thread() = this;
            while (! threadShouldExit())
                m_service.run_one();
        }
    };

    //--------------------------------------------------------------------------

    template <typename ServiceType>
    struct Producer : Thread
    {
        ServiceType& m_service;
        Random m_random;
        String m_string;

        Producer (int id, int64 seedValue, ServiceType& service)
            : Thread ("P#" + String::fromNumber (id))
            , m_service (service)
            , m_random (seedValue)
            { }

        ~Producer ()
            { stopThread(); }

        void run ()
        {
            for (std::size_t i = 0; i < callsPerThread; ++i)
            {
                String const s (String::fromNumber (m_random.nextInt()));
                m_string += s;
                if (m_string.length() > 100)
                    m_string = String::empty;
                m_service.dispatch (bind (&Consumer<ServiceType>::handler));
            }
        }
    };

    //--------------------------------------------------------------------------

    template <typename Allocator>
    void testThreads (std::size_t nConsumers, std::size_t nProducers)
    {
        beginTestCase (String::fromNumber (nConsumers) + " consumers, " +
                       String::fromNumber (nProducers) + " producers, " +
                       "Allocator = " + std::string(typeid(Allocator).name()));

        typedef ServiceQueueType <Allocator> ServiceType;

        ServiceType service (nConsumers);
        std::vector <ScopedPointer <Consumer <ServiceType> > > consumers;
        std::vector <ScopedPointer <Producer <ServiceType> > > producers;
        consumers.reserve (nConsumers);
        producers.reserve (nProducers);

        for (std::size_t i = 0; i < nConsumers; ++i)
            consumers.push_back (new Consumer <ServiceType> (i + 1,
                random().nextInt64(), service));

        for (std::size_t i = 0; i < nProducers; ++i)
            producers.push_back (new Producer <ServiceType> (i + 1,
                random().nextInt64(), service));

        Stopwatch t;
        
        for (std::size_t i = 0; i < producers.size(); ++i)
            producers[i]->startThread();
        
        for (std::size_t i = 0; i < producers.size(); ++i)
            producers[i]->waitForThreadToExit();

        for (std::size_t i = 0; i < consumers.size(); ++i)
            service.dispatch (bind (&Consumer <ServiceType>::stop_one));

        for (std::size_t i = 0; i < consumers.size(); ++i)
            consumers[i]->waitForThreadToExit();

        double const seconds (t.getElapsed());
        logMessage (String (seconds, 2) + " seconds");

        pass();
    }

    void runTest()
    {
#if 1
        testThreads <std::allocator<char> > (1, 1);
        testThreads <std::allocator<char> > (1, 4);
        testThreads <std::allocator<char> > (1, 16);
        testThreads <std::allocator<char> > (4, 1);
        testThreads <std::allocator<char> > (8, 16);
#endif

#if 0
        testThreads <detail::ServiceQueueAllocator<char> > (1, 1);
        testThreads <detail::ServiceQueueAllocator<char> > (1, 4);
        testThreads <detail::ServiceQueueAllocator<char> > (1, 16);
        testThreads <detail::ServiceQueueAllocator<char> > (4, 1);
        testThreads <detail::ServiceQueueAllocator<char> > (8, 16);
#endif
    }

    ServiceQueueTimingTests () : UnitTest ("ServiceQueueTiming", "beast", runManual)
    {
    }
};

static ServiceQueueTimingTests serviceQueueTimingTests;

//------------------------------------------------------------------------------

class ServiceQueueTests
    : public UnitTest
{
public:
    struct ServiceThread : Thread
    {
        Random m_random;
        ServiceQueue& m_service;
        String m_string;

        ServiceThread (int id, int64 seedValue,
            ServiceQueue& service)
            : Thread ("#" + String::fromNumber (id))
            , m_random (seedValue)
            , m_service (service)
        {
            startThread();
        }

        ~ServiceThread ()
        {
            stopThread();
        }

        static ServiceThread*& thread()
        {
            static ThreadLocalValue <ServiceThread*> local;
            return local.get();
        }

        static void stop_one ()
        {
            thread()->signalThreadShouldExit();
        }

        static void handler ()
        {
            thread()->do_handler();
        }

        void do_handler()
        {
#if 1
            String const s (String::fromNumber (m_random.nextInt()));
            m_string += s;
            if (m_string.length() > 100)
                m_string = String::empty;
#endif
        }

        void run ()
        {
            thread() = this;
            while (! threadShouldExit())
                m_service.run_one();
        }
    };

    static int const callsPerThread = 10000;

    void testThreads (std::size_t n)
    {
        beginTestCase (String::fromNumber (n) + " threads");
        ServiceQueue service (n);
        std::vector <ScopedPointer <ServiceThread> > threads;
        threads.reserve (n);
        for (std::size_t i = 0; i < n; ++i)
            threads.push_back (new ServiceThread (i + 1,
                random().nextInt64(), service));
        for (std::size_t i = n * callsPerThread; i; --i)
            service.dispatch (bind (&ServiceThread::handler));
        for (std::size_t i = 0; i < threads.size(); ++i)
            service.dispatch (bind (&ServiceThread::stop_one));
        for (std::size_t i = 0; i < threads.size(); ++i)
            threads[i]->waitForThreadToExit();
        pass();
    }

    void runTest()
    {
        testThreads (1);
        testThreads (4);
        testThreads (16);
    }

    ServiceQueueTests () : UnitTest ("ServiceQueue", "beast")
    {
    }
};

static ServiceQueueTests serviceQueueTests;

}

}
