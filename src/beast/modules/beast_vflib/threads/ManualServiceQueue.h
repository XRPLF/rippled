//------------------------------------------------------------------------------
/*
	This file is part of Beast: https://github.com/vinniefalco/Beast
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

#ifndef BEAST_VFLIB_MANUALSERVICEQUEUE_H_INCLUDED
#define BEAST_VFLIB_MANUALSERVICEQUEUE_H_INCLUDED

#include "CallQueue.h"

namespace beast {
	
class ManualServiceQueue
: public CallQueue
{
public:
	explicit ManualServiceQueue (const String& name)
    : CallQueue(name)
    {
    }
	
	/** Calls all functors in the queue. Returns if there are no
	    more functors available to run
	*/
	bool synchronize ()
    {
        if(poll() > 0)
            return true;

        return false;
    }
};

//------------------------------------------------------------------------------

namespace detail
{

//------------------------------------------------------------------------------

class ManualServiceQueueTests
: public UnitTest
{
public:
	struct CallTracker
    {
        ManualServiceQueueTests *unitTest;
        int c0, c1;
        int q0, q1;

		CallTracker(ManualServiceQueueTests *parent)
		: unitTest(parent)
        , c0(0), c1(0)
		, q0(0), q1(0)
		{
		}
        
        void doQ0() { q0++; }
        
        void doQ1(const String& p1)
        {
            unitTest->expect(p1 == "p1");
            q1++;
        }

        void doC0() { c0++; }
        
        void doC1(const String& p1)
        {
            unitTest->expect(p1 == "p1");
            c1++;
        }
    };

    void performEmptyQueue()
    {
		beginTestCase("Empty queue");

        ManualServiceQueue queue("ManualServiceQueueTests");

        bool doneSomething = queue.synchronize();
        expect(!doneSomething);

        queue.close();
    }

	void performCalls()
	{
		beginTestCase("Calls");

		Random r;
		r.setSeedRandomly();
		
        ManualServiceQueue queue("ManualServiceQueueTests");
		
        static int const batches = 1000;

		for(std::size_t i=0; i<batches; i++)
		{
            CallTracker ct(this);

		    std::size_t batchSize = r.nextLargeNumber(10).toInteger();
			
            if(batchSize > 0)
            {
                for(std::size_t y=0; y<batchSize; y++)
                {
                    int callType = r.nextLargeNumber(10).toInteger();

			        if(callType % 2)
                    {
                        queue.queue(&CallTracker::doQ0, &ct);
                        queue.call(&CallTracker::doC0, &ct);
                    }
			        else
                    {
				        queue.queue(&CallTracker::doQ1, &ct, "p1");
				        queue.call(&CallTracker::doC1, &ct, "p1");
                    }
                }

                bool doneSomething = queue.synchronize();
            
                expect(doneSomething);
                expect ((ct.q0 + ct.q1) == batchSize);
                expect ((ct.c0 + ct.c1) == batchSize);

                doneSomething = queue.synchronize();
                expect(!doneSomething);
            }
		}

        queue.close ();
	}

	void runTest()
	{      
        performEmptyQueue ();
        performCalls ();
	}

	ManualServiceQueueTests () : UnitTest ("ManualServiceQueue", "beast")
	{
	}
};

}

}

#endif
