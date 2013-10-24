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

#include "ThreadWithServiceQueue.h"

namespace beast {

ThreadWithServiceQueue::ThreadWithServiceQueue (const String& name)
: CallQueue(name)
, m_thread(name, this)
, m_entryPoints(nullptr)
, m_calledStart(false)
, m_calledStop(false)
{
}

ThreadWithServiceQueue::~ThreadWithServiceQueue ()
{
	stop(true);
}

void ThreadWithServiceQueue::start (EntryPoints* const entryPoints)
{
	// start() MUST be called.
	bassert (!m_calledStart);
	m_calledStart = true;
	
	m_entryPoints = entryPoints;
	
	m_thread.startThread ();
}

void ThreadWithServiceQueue::stop (bool const wait)
{
	// start() MUST be called.
	bassert (m_calledStart);
	
	if (!m_calledStop)
	{
		m_calledStop = true;
		
		m_thread.signalThreadShouldExit();
		
		// something could slip in here
		
		close ();
	}
	
	if (wait)
		m_thread.waitForThreadToExit ();
}

void ThreadWithServiceQueue::runThread ()
{
	m_entryPoints->threadInit ();
	
	while (! m_thread.threadShouldExit ())
        run ();
    
    // Perform the remaining calls in the queue
    
    reset ();
    poll ();
	
	m_entryPoints->threadExit();
}

//------------------------------------------------------------------------------

namespace detail
{

//------------------------------------------------------------------------------

class ThreadWithServiceQueueTests
: public UnitTest
{
public:
	
	struct ThreadWithServiceQueueRunner
	: public ThreadWithServiceQueue::EntryPoints
	{
		ThreadWithServiceQueue m_worker;
		int cCallCount, c1CallCount;
		int qCallCount, q1CallCount;
		int initCalled, exitCalled;
		
		ThreadWithServiceQueueRunner()
		: m_worker("ThreadWithServiceQueueRunner")
		, cCallCount(0)
		, c1CallCount(0)
		, qCallCount(0)
		, q1CallCount(0)
		, initCalled(0)
		, exitCalled(0)
		{
		}
		
		void start()
		{
			m_worker.start(this);
		}
		
		void stop()
		{
			m_worker.stop(true);
		}
		
		void c()
		{
			m_worker.call(&ThreadWithServiceQueueRunner::cImpl, this);
		}
		
		void cImpl()
		{
			cCallCount++;
		}
		
		void c1(int p1)
		{
			m_worker.call(&ThreadWithServiceQueueRunner::c1Impl, this, p1);
		}
		
		void c1Impl(int p1)
		{
			c1CallCount++;
		}

		void q()
		{
			m_worker.queue(&ThreadWithServiceQueueRunner::qImpl, this);
		}
		
		void qImpl()
		{
			qCallCount++;
		}
		
		void q1(int p1)
		{
			m_worker.queue(&ThreadWithServiceQueueRunner::q1Impl, this, p1);
		}
		
		void q1Impl(int p1)
		{
			q1CallCount++;
		}

		void threadInit ()
		{
			initCalled++;
		}
        
		void threadExit ()
		{
			exitCalled++;
		}
	};
		
	static int const calls = 1000;
	
	void performCalls()
	{
		Random r;
		r.setSeedRandomly();
		
		ThreadWithServiceQueueRunner runner;
								
		runner.start();
		
		for(std::size_t i=0; i<calls; i++)
		{
			int wait = r.nextLargeNumber(10).toInteger();
			
			if(wait % 2)
				runner.q();
			else
				runner.q1Impl(wait);
		}

		for(std::size_t i=0; i<calls; i++)
		{
			int wait = r.nextLargeNumber(10).toInteger();
			
			if(wait % 2)
				runner.c();
			else
				runner.c1Impl(wait);
		}
		
		runner.stop();
		
		expect ((runner.cCallCount + runner.c1CallCount) == calls);
		expect ((runner.qCallCount + runner.q1CallCount) == calls);
		expect (runner.initCalled == 1);
		expect (runner.exitCalled == 1);
	}
	
	void runTest()
	{
		beginTestCase("Calls");
        
        for(int i=0; i<100; i++)
            performCalls ();
	}
	
	ThreadWithServiceQueueTests () : UnitTest ("ThreadWithServiceQueue", "beast")
	{
	}
};

static CallQueueTests callQueueTests;

static ThreadWithServiceQueueTests bindableServiceQueueTests;

}

}
