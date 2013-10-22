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

#ifndef BEAST_VFLIB_THREADWITHSERVICEQUEUE_H_INCLUDED
#define BEAST_VFLIB_THREADWITHSERVICEQUEUE_H_INCLUDED

#include "BindableServiceQueue.h"

namespace beast {
	
	/** TODO: Queued calls no longer interrupt the idle method at the moment
	 use an explicit call to interrupt() if you want to also interrupt the
	 idle method when queuing calls
	 */
	
	class ThreadWithServiceQueue
	: public BindableServiceQueue
	, public Thread
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
			
			virtual bool threadIdle ()
			{
				bool interrupted = false;
				
				return interrupted;
			}
		};
		
		explicit ThreadWithServiceQueue (const String& name);
		
		~ThreadWithServiceQueue();
		
		void start (EntryPoints* const entryPoints);
		
		void stop (bool const wait);
		
		// Should be called periodically by the idle function.
		// There are two possible results:
		//
		// #1 Returns false. The idle function may continue or return.
		// #2 Returns true. The idle function should return as soon as possible.
		//
		// May only be called on the service queue thead
		bool interruptionPoint ();

		/* Interrupts the idle function.
		 */
		void interrupt ();
		
	private:
		void run ();
		void doInterrupt ();
		void doWakeUp ();
		
	private:
		EntryPoints* m_entryPoints;
		bool m_calledStart;
		bool m_calledStop;
		bool m_interrupted;
		CriticalSection m_mutex;
	};
}

#endif
