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

#include <ripple/core/impl/Workers.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

//------------------------------------------------------------------------------

class Workers_test : public beast::unit_test::suite
{
public:
	struct TestCallback : Workers::Callback
	{
		explicit TestCallback(int count_)
			: finished(false, count_ == 0)
			, count(count_)
		{
		}

		void processTask()
		{
			if (--count == 0)
				finished.signal();
		}

		beast::WaitableEvent finished;
		std::atomic <int> count;
	};

	void testThreads(int const threadCount)
	{
		testcase("threadCount = " + std::to_string(threadCount));

		TestCallback cb(threadCount);

		Workers w(cb, "Test", 0);
		BEAST_EXPECT(w.getNumberOfThreads() == 0);

		w.setNumberOfThreads(threadCount);
		BEAST_EXPECT(w.getNumberOfThreads() == threadCount);

		for (int i = 0; i < threadCount; ++i)
			w.addTask();

		// 10 seconds should be enough to finish on any system
		//
		bool signaled = cb.finished.wait(10 * 1000);
		BEAST_EXPECT(signaled);

		w.pauseAllThreadsAndWait();

		// We had better finished all our work!
		BEAST_EXPECT(cb.count.load() == 0);
	}

	void run()
	{
		testThreads(0);
		testThreads(1);
		testThreads(2);
		testThreads(4);
		testThreads(16);
		testThreads(64);
	}
};

BEAST_DEFINE_TESTSUITE(Workers, core, ripple);

}