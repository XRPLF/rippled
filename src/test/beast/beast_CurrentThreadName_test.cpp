#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/unit_test.h>

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

BEAST_DEFINE_TESTSUITE(CurrentThreadName, beast, beast);

}  // namespace test
}  // namespace ripple
