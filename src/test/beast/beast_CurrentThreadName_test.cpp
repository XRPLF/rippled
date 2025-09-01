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

#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/unit_test.h>

#include <boost/predef/os/linux.h>

#include <thread>

namespace ripple {
namespace test {

class CurrentThreadName_test : public beast::unit_test::suite
{
private:
    static void
    exerciseName(
        std::string myName,
        std::atomic<bool>* stop,
        std::atomic<int>* state)
    {
        // Verify that upon creation a thread has no name.
        auto const initialThreadName = beast::getCurrentThreadName();

        // Set the new name.
        beast::setCurrentThreadName(myName);

        // Indicate to caller that the name is set.
        *state = 1;

        // If there is an initial thread name then we failed.
        if (!initialThreadName.empty())
            return;

        // Wait until all threads have their names.
        while (!*stop)
            ;

        // Make sure the thread name that we set before is still there
        // (not overwritten by, for instance, another thread).
        if (beast::getCurrentThreadName() == myName)
            *state = 2;
    }
#if BOOST_OS_LINUX
    // Helper function to test a specific name.
    // It creates a thread, sets the name, and checks if the OS-level
    // name matches the expected (potentially truncated) name.
    void
    testName(std::string const& nameToSet, std::string const& expectedName)
    {
        constexpr static std::size_t maxThreadNameLen = 15;
        std::thread t([&] {
            beast::setCurrentThreadName(nameToSet);

            // Initialize buffer to be safe.
            char actualName[maxThreadNameLen + 1] = {};
            pthread_getname_np(pthread_self(), actualName, sizeof(actualName));

            BEAST_EXPECT(std::string(actualName) == expectedName);
        });
        t.join();
    }
#endif

public:
    void
    run() override
    {
        // Test 1: Make two different threads with two different names.
        // Make sure that the expected thread names are still there
        // when the thread exits.
        {
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
#if BOOST_OS_LINUX
        // Test 2: On Linux, verify that thread names longer than 15 characters
        // are truncated to 15 characters (the 16th character is reserved for
        // the null terminator).
        {
            testName(
                "123456789012345",
                "123456789012345");  // 15 chars, no truncation
            testName(
                "1234567890123456", "123456789012345");  // 16 chars, truncated
            testName(
                "ThisIsAVeryLongThreadNameExceedingLimit",
                "ThisIsAVeryLong");      // 39 chars, truncated
            testName("", "");            // empty name
            testName("short", "short");  // short name, no truncation
        }
#endif
    }
};

BEAST_DEFINE_TESTSUITE(CurrentThreadName, beast, beast);

}  // namespace test
}  // namespace ripple
