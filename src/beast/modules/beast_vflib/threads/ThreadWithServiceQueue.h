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

#ifndef BEAST_VFLIB_THREADWITHSERVICEQUEUE_H_INCLUDED
#define BEAST_VFLIB_THREADWITHSERVICEQUEUE_H_INCLUDED

#include "CallQueue.h"

namespace beast {
	
class ThreadWithServiceQueue
: public CallQueue
{
public:
	/** Entry points for a ThreadWithCallQueue.
		*/
	class EntryPoints
	{
	public:
		virtual ~EntryPoints () { }
			
		virtual void threadInit () { }
			
		virtual void threadExit () { }
	};
	
	explicit ThreadWithServiceQueue (const String& name);
	
	~ThreadWithServiceQueue();
	
	void start (EntryPoints* const entryPoints);
	
	void stop (bool const wait);
	
	/** Calls all functors in the queue. Blocks if there are no
	    functors available to run until more functors become
		available or the queue is stopped

	*/
	bool synchronize ();

	/** Helper class to work around ThreadWithServiceQueue and Thread both
	    having a run member
	*/
	class Worker
	: public Thread
	{
	public:
		Worker(const String& name, ThreadWithServiceQueue *parent)
		: Thread(name)
		, m_parent(parent)
		{
		}

		void run()
		{
			m_parent->runThread();
		}

	private:
		ThreadWithServiceQueue *m_parent;
	};

	void runThread ();
	
private:
	EntryPoints* m_entryPoints;
	bool m_calledStart;
	bool m_calledStop;
	Worker m_thread;
};

}

#endif
