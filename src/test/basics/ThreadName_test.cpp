//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/basics/ThreadUtilities.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class ThreadName_test : public beast::unit_test::suite
{
private:
    static void
    exerciseName(
        std::string myName,
        std::atomic<bool>* stop,
        std::atomic<int>* state)
    {
        // Set the new name.
        this_thread::set_name(myName);

        // Indicate to caller that the name is set.
        *state = 1;

        // Wait until all threads have their names.
        while (!*stop)
            ;

        // Make sure the thread name that we set before is still there
        // (not overwritten by, for instance, another thread).
        if (this_thread::get_name() == myName)
            *state = 2;
    }

public:
    void
    run() override
    {
        // Make two different threads with two different names.  Make sure
        // that the expected thread names are still there when the thread
        // exits.
        std::atomic<bool> stop{false};

        std::atomic<int> stateA{0};
        std::thread tA(exerciseName, "tA", &stop, &stateA);

        std::atomic<int> stateB{0};
        std::thread tB(exerciseName, "tB", &stop, &stateB);

        // Wait until both threads have set their names.
        while (stateA == 0 || stateB == 0)
            ;

        stop = true;
        tA.join();
        tB.join();

        // Both threads should still have the expected name when they exit.
        BEAST_EXPECT(stateA == 2);
        BEAST_EXPECT(stateB == 2);
    }
};

BEAST_DEFINE_TESTSUITE(ThreadName, basics, ripple);

}  // namespace test
}  // namespace ripple
