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

#include <beast/threads/ServiceQueue.h>

#include <beast/unit_test/suite.h>

#include <beast/module/core/time/Time.h>
#include <beast/module/core/maths/Random.h>

#include <functional>
#include <sstream>

namespace beast {

class ServiceQueue_timing_test : public unit_test::suite
{
public:
    class Stopwatch
    {
    public:
        Stopwatch () { start(); }
        void start () { m_startTime = Time::getHighResolutionTicks (); }
        double getElapsed ()
        {
            std::int64_t const now = Time::getHighResolutionTicks();
            return Time::highResolutionTicksToSeconds (now - m_startTime);
        }
    private:
        std::int64_t m_startTime;
    };

    static int const callsPerThread = 50000;

    //--------------------------------------------------------------------------

    template <typename ServiceType>
    struct Consumer : Thread
    {
        ServiceType& m_service;
        Random m_random;
        String m_string;

        Consumer (int id, std::int64_t seedValue, ServiceType& service)
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

        Producer (int id, std::int64_t seedValue, ServiceType& service)
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
                m_service.dispatch (std::bind (&Consumer<ServiceType>::handler));
            }
        }
    };

    //--------------------------------------------------------------------------

    template <typename Allocator>
    void testThreads (int nConsumers, int nProducers)
    {
        std::stringstream ss;
        ss << 
            nConsumers << " consumers, " <<
            nProducers << " producers, Allocator = " <<
            typeid(Allocator).name();
        testcase (ss.str());

        typedef ServiceQueueType <Allocator> ServiceType;

        ServiceType service (nConsumers);
        std::vector <std::unique_ptr <Consumer <ServiceType> > > consumers;
        std::vector <std::unique_ptr <Producer <ServiceType> > > producers;
        consumers.reserve (nConsumers);
        producers.reserve (nProducers);

        Random r;

        for (int i = 0; i < nConsumers; ++i)
            consumers.emplace_back (new Consumer <ServiceType> (i + 1,
                r.nextInt64(), service));

        for (int i = 0; i < nProducers; ++i)
            producers.emplace_back (new Producer <ServiceType> (i + 1,
                r.nextInt64(), service));

        Stopwatch t;
        
        for (std::size_t i = 0; i < producers.size(); ++i)
            producers[i]->startThread();
        
        for (std::size_t i = 0; i < producers.size(); ++i)
            producers[i]->waitForThreadToExit();

        for (std::size_t i = 0; i < consumers.size(); ++i)
            service.dispatch (std::bind (&Consumer <ServiceType>::stop_one));

        for (std::size_t i = 0; i < consumers.size(); ++i)
            consumers[i]->waitForThreadToExit();

        double const seconds (t.getElapsed());
        log << seconds << " seconds";

        pass();
    }

    void run()
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
};

BEAST_DEFINE_TESTSUITE_MANUAL(ServiceQueue_timing,threads,beast);

//------------------------------------------------------------------------------

class ServiceQueue_test : public unit_test::suite
{
public:
    struct ServiceThread : Thread
    {
        Random m_random;
        ServiceQueue& m_service;
        String m_string;

        ServiceThread (int id, std::int64_t seedValue,
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

    static std::size_t const totalCalls = 10000;

    void testThreads (int n)
    {
        std::stringstream ss;
        ss << n << " threads";
        testcase (ss.str());

        Random r;
        std::size_t const callsPerThread (totalCalls / n);

        ServiceQueue service (n);
        std::vector <std::unique_ptr <ServiceThread> > threads;
        threads.reserve (n);
        for (int i = 0; i < n; ++i)
            threads.emplace_back (new ServiceThread (i + 1,
                r.nextInt64(), service));
        for (std::size_t i = n * callsPerThread; i; --i)
            service.dispatch (std::bind (&ServiceThread::handler));
        for (std::size_t i = 0; i < threads.size(); ++i)
            service.dispatch (std::bind (&ServiceThread::stop_one));
        for (std::size_t i = 0; i < threads.size(); ++i)
            threads[i]->waitForThreadToExit();
        pass();
    }

    void run()
    {
        testThreads (1);
        testThreads (4);
        testThreads (16);
    }
};

BEAST_DEFINE_TESTSUITE(ServiceQueue,threads,beast);

}
