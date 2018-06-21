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

#include <ripple/core/ClosureCounter.h>
#include <ripple/beast/unit_test.h>
#include <test/jtx/Env.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace ripple {
namespace test {

//------------------------------------------------------------------------------

class ClosureCounter_test : public beast::unit_test::suite
{
    // We're only using Env for its Journal.
    jtx::Env env {*this};
    beast::Journal j {env.app().journal ("ClosureCounter_test")};

    void testConstruction()
    {
        // Build different kinds of ClosureCounters.
        {
            // Count closures that return void and take no arguments.
            ClosureCounter<void> voidCounter;
            BEAST_EXPECT (voidCounter.count() == 0);

            int evidence = 0;
            // Make sure voidCounter.wrap works with an rvalue closure.
            auto wrapped = voidCounter.wrap ([&evidence] () { ++evidence; });
            BEAST_EXPECT (voidCounter.count() == 1);
            BEAST_EXPECT (evidence == 0);
            BEAST_EXPECT (wrapped);

            // wrapped() should be callable with no arguments.
            (*wrapped)();
            BEAST_EXPECT (evidence == 1);
            (*wrapped)();
            BEAST_EXPECT (evidence == 2);

            // Destroying the contents of wrapped should decrement voidCounter.
            wrapped = boost::none;
            BEAST_EXPECT (voidCounter.count() == 0);
        }
        {
            // Count closures that return void and take one int argument.
            ClosureCounter<void, int> setCounter;
            BEAST_EXPECT (setCounter.count() == 0);

            int evidence = 0;
            // Make sure setCounter.wrap works with a non-const lvalue closure.
            auto setInt = [&evidence] (int i) { evidence = i; };
            auto wrapped = setCounter.wrap (setInt);

            BEAST_EXPECT (setCounter.count() == 1);
            BEAST_EXPECT (evidence == 0);
            BEAST_EXPECT (wrapped);

            // wrapped() should be callable with one integer argument.
            (*wrapped)(5);
            BEAST_EXPECT (evidence == 5);
            (*wrapped)(11);
            BEAST_EXPECT (evidence == 11);

            // Destroying the contents of wrapped should decrement setCounter.
            wrapped = boost::none;
            BEAST_EXPECT (setCounter.count() == 0);
        }
        {
            // Count closures that return int and take two int arguments.
            ClosureCounter<int, int, int> sumCounter;
            BEAST_EXPECT (sumCounter.count() == 0);

            // Make sure sumCounter.wrap works with a const lvalue closure.
            auto const sum = [] (int i, int j) { return i + j; };
            auto wrapped = sumCounter.wrap (sum);

            BEAST_EXPECT (sumCounter.count() == 1);
            BEAST_EXPECT (wrapped);

            // wrapped() should be callable with two integers.
            BEAST_EXPECT ((*wrapped)(5,  2) ==  7);
            BEAST_EXPECT ((*wrapped)(2, -8) == -6);

            // Destroying the contents of wrapped should decrement sumCounter.
            wrapped = boost::none;
            BEAST_EXPECT (sumCounter.count() == 0);
        }
    }

    // A class used to test argument passing.
    class TrackedString
    {
    public:
        int copies = {0};
        int moves = {0};
        std::string str;

        TrackedString() = delete;

        explicit TrackedString(char const* rhs)
        : str (rhs) {}

        // Copy constructor
        TrackedString (TrackedString const& rhs)
        : copies (rhs.copies + 1)
        , moves (rhs.moves)
        , str (rhs.str) {}

        // Move constructor
        TrackedString (TrackedString&& rhs) noexcept
        : copies (rhs.copies)
        , moves (rhs.moves + 1)
        , str (std::move(rhs.str)) {}

        // Delete copy and move assignment.
        TrackedString& operator=(TrackedString const& rhs) = delete;

        // String concatenation
        TrackedString& operator+=(char const* rhs)
        {
            str += rhs;
            return *this;
        }

        friend
        TrackedString operator+(TrackedString const& str, char const* rhs)
        {
            TrackedString ret {str};
            ret.str += rhs;
            return ret;
        }
    };

    void testArgs()
    {
        // Make sure a wrapped closure handles rvalue reference arguments
        // correctly.
        {
            // Pass by value.
            ClosureCounter<TrackedString, TrackedString> strCounter;
            BEAST_EXPECT (strCounter.count() == 0);

            auto wrapped = strCounter.wrap (
                [] (TrackedString in) { return in += "!"; });

            BEAST_EXPECT (strCounter.count() == 1);
            BEAST_EXPECT (wrapped);

            TrackedString const strValue ("value");
            TrackedString const result = (*wrapped)(strValue);
            BEAST_EXPECT (result.copies == 2);
            BEAST_EXPECT (result.moves == 1);
            BEAST_EXPECT (result.str == "value!");
            BEAST_EXPECT (strValue.str.size() == 5);
        }
        {
            // Use a const lvalue argument.
            ClosureCounter<TrackedString, TrackedString const&> strCounter;
            BEAST_EXPECT (strCounter.count() == 0);

            auto wrapped = strCounter.wrap (
                [] (TrackedString const& in) { return in + "!"; });

            BEAST_EXPECT (strCounter.count() == 1);
            BEAST_EXPECT (wrapped);

            TrackedString const strConstLValue ("const lvalue");
            TrackedString const result = (*wrapped)(strConstLValue);
            BEAST_EXPECT (result.copies == 1);
            // BEAST_EXPECT (result.moves == ?); // moves VS == 1, gcc == 0
            BEAST_EXPECT (result.str == "const lvalue!");
            BEAST_EXPECT (strConstLValue.str.size() == 12);
        }
        {
            // Use a non-const lvalue argument.
            ClosureCounter<TrackedString, TrackedString&> strCounter;
            BEAST_EXPECT (strCounter.count() == 0);

            auto wrapped = strCounter.wrap (
                [] (TrackedString& in) { return in += "!"; });

            BEAST_EXPECT (strCounter.count() == 1);
            BEAST_EXPECT (wrapped);

            TrackedString strLValue ("lvalue");
            TrackedString const result = (*wrapped)(strLValue);
            BEAST_EXPECT (result.copies == 1);
            BEAST_EXPECT (result.moves == 0);
            BEAST_EXPECT (result.str == "lvalue!");
            BEAST_EXPECT (strLValue.str == result.str);
        }
        {
            // Use an rvalue argument.
            ClosureCounter<TrackedString, TrackedString&&> strCounter;
            BEAST_EXPECT (strCounter.count() == 0);

            auto wrapped = strCounter.wrap (
                [] (TrackedString&& in) {
                    // Note that none of the compilers noticed that in was
                    // leaving scope.  So, without intervention, they would
                    // do a copy for the return (June 2017).  An explicit
                    // std::move() was required.
                    return std::move(in += "!");
                });

            BEAST_EXPECT (strCounter.count() == 1);
            BEAST_EXPECT (wrapped);

            // Make the string big enough to (probably) avoid the small string
            // optimization.
            TrackedString strRValue ("rvalue abcdefghijklmnopqrstuvwxyz");
            TrackedString const result = (*wrapped)(std::move(strRValue));
            BEAST_EXPECT (result.copies == 0);
            BEAST_EXPECT (result.moves == 1);
            BEAST_EXPECT (result.str == "rvalue abcdefghijklmnopqrstuvwxyz!");
            BEAST_EXPECT (strRValue.str.size() == 0);
        }
    }

    void testWrap()
    {
        // Verify reference counting.
        ClosureCounter<void> voidCounter;
        BEAST_EXPECT (voidCounter.count() == 0);
        {
            auto wrapped1 = voidCounter.wrap ([] () {});
            BEAST_EXPECT (voidCounter.count() == 1);
            {
                // Copy should increase reference count.
                auto wrapped2 (wrapped1);
                BEAST_EXPECT (voidCounter.count() == 2);
                {
                    // Move should increase reference count.
                    auto wrapped3 (std::move(wrapped2));
                    BEAST_EXPECT (voidCounter.count() == 3);
                    {
                        // An additional closure also increases count.
                        auto wrapped4 = voidCounter.wrap ([] () {});
                        BEAST_EXPECT (voidCounter.count() == 4);
                    }
                    BEAST_EXPECT (voidCounter.count() == 3);
                }
                BEAST_EXPECT (voidCounter.count() == 2);
            }
            BEAST_EXPECT (voidCounter.count() == 1);
        }
        BEAST_EXPECT (voidCounter.count() == 0);

        // Join with 0 count should not stall.
        using namespace std::chrono_literals;
        voidCounter.join("testWrap", 1ms, j);

        // Wrapping a closure after join() should return boost::none.
        BEAST_EXPECT (voidCounter.wrap ([] () {}) == boost::none);
    }

    void testWaitOnJoin()
    {
        // Verify reference counting.
        ClosureCounter<void> voidCounter;
        BEAST_EXPECT (voidCounter.count() == 0);

        auto wrapped = (voidCounter.wrap ([] () {}));
        BEAST_EXPECT (voidCounter.count() == 1);

        // Calling join() now should stall, so do it on a different thread.
        std::atomic<bool> threadExited {false};
        std::thread localThread ([&voidCounter, &threadExited, this] ()
        {
            // Should stall after calling join.
            using namespace std::chrono_literals;
            voidCounter.join("testWaitOnJoin", 1ms, j);
            threadExited.store (true);
        });

        // Wait for the thread to call voidCounter.join().
        while (! voidCounter.joined());

        // The thread should still be active after waiting 5 milliseconds.
        // This is not a guarantee that join() stalled the thread, but it
        // improves confidence.
        using namespace std::chrono_literals;
        std::this_thread::sleep_for (5ms);
        BEAST_EXPECT (threadExited == false);

        // Destroy the contents of wrapped and expect the thread to exit
        // (asynchronously).
        wrapped = boost::none;
        BEAST_EXPECT (voidCounter.count() == 0);

        // Wait for the thread to exit.
        while (threadExited == false);
        localThread.join();
    }

public:
    void run() override
    {
        testConstruction();
        testArgs();
        testWrap();
        testWaitOnJoin();
    }
};

BEAST_DEFINE_TESTSUITE(ClosureCounter, core, ripple);

} // test
} // ripple
