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

#include "ThreadWithServiceQueue.h"

namespace beast {

ThreadWithServiceQueue::ThreadWithServiceQueue (const String& name)
: Thread (name)
, m_entryPoints(nullptr)
, m_calledStart(false)
, m_calledStop(false)
, m_interrupted(false)
{
}

ThreadWithServiceQueue::~ThreadWithServiceQueue()
{
	stop(true);
}

void ThreadWithServiceQueue::start (EntryPoints* const entryPoints)
{
	{
		CriticalSection::ScopedLockType lock (m_mutex);
		
		// start() MUST be called.
		bassert (!m_calledStart);
		m_calledStart = true;
	}
	
	m_entryPoints = entryPoints;
	
	startThread();
}

void ThreadWithServiceQueue::stop (bool const wait)
{
	{
		CriticalSection::ScopedLockType lock (m_mutex);
		
		// start() MUST be called.
		bassert (m_calledStart);
		
		if (!m_calledStop)
		{
			m_calledStop = true;
			
			{
				CriticalSection::ScopedUnlockType unlock (m_mutex);
				
				call (&Thread::signalThreadShouldExit, this);
				
				// something could slip in here
				
				// m_queue.close();
			}
		}
	}
	
	if (wait)
		waitForThreadToExit();
}
	
void ThreadWithServiceQueue::interrupt ()
{
	call (&ThreadWithServiceQueue::doInterrupt, this);
}

bool ThreadWithServiceQueue::interruptionPoint ()
{
	return m_interrupted;
}

void ThreadWithServiceQueue::run ()
{
	m_entryPoints->threadInit();
	
	while (! this->threadShouldExit())
	{
		run_one();
		
		bool isInterrupted = m_entryPoints->threadIdle();
		
		isInterrupted |= interruptionPoint();
		
		if(isInterrupted)
		{
			// We put this call into the service queue to make
			// sure we get through to threadIdle without
			// waiting
			call (&ThreadWithServiceQueue::doWakeUp, this);
		}
	}
	
	m_entryPoints->threadExit();
}

void ThreadWithServiceQueue::doInterrupt ()
{
	m_interrupted = true;
}

void ThreadWithServiceQueue::doWakeUp ()
{
	m_interrupted = false;
}

//------------------------------------------------------------------------------

namespace detail
{

//------------------------------------------------------------------------------

class BindableServiceQueueTests
: public UnitTest
{
public:
	
	struct BindableServiceQueueRunner
	: public ThreadWithServiceQueue::EntryPoints
	{
		ThreadWithServiceQueue m_worker;
		int cCallCount, c1CallCount, idleInterruptedCount;
		
		BindableServiceQueueRunner()
		: m_worker("BindableServiceQueueRunner")
		, cCallCount(0)
		, c1CallCount(0)
		, idleInterruptedCount(0)
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
		
		void interrupt()
		{
			m_worker.interrupt();
		}
		
		void c()
		{
			m_worker.queue(&BindableServiceQueueRunner::cImpl, this);
		}
		
		void cImpl()
		{
			cCallCount++;
		}
		
		void c1(int p1)
		{
			m_worker.queue(&BindableServiceQueueRunner::c1Impl, this, p1);
		}
		
		void c1Impl(int p1)
		{
			c1CallCount++;
		}
		
		bool threadIdle ()
		{
			bool interrupted = m_worker.interruptionPoint ();
			
			if(interrupted)
				idleInterruptedCount++;
			
			return interrupted;
		}
	};
		
	static int const calls = 10000;
	
	void performCalls()
	{
		Random r;
		r.setSeedRandomly();
		
		BindableServiceQueueRunner runner;
		
		beginTestCase("Calls and interruptions");
								
		runner.start();
		
		for(std::size_t i=0; i<calls; i++)
		{
			int wait = r.nextLargeNumber(10).toInteger();
			
			if(wait % 2)
				runner.c();
			else
				runner.c1Impl(wait);
		}
						
		for(std::size_t i=0; i<calls; i++)
			runner.interrupt();
		
		runner.stop();
		
		// We can only reason that the idle method must have been interrupted
		// at least once
		expect ((runner.cCallCount + runner.c1CallCount) == calls);
		expect (runner.idleInterruptedCount > 0);
	}
	
	void runTest()
	{
		performCalls ();
	}
	
	BindableServiceQueueTests () : UnitTest ("BindableServiceQueue", "beast")
	{
	}
};

static BindableServiceQueueTests bindableServiceQueueTests;
	
}


}
