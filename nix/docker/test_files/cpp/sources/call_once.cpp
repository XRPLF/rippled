// An exception thrown from a std::call_once callable must propagate to the
// caller and leave the flag unset, so a later call runs its callable. Two
// things have broken this:
//  - glibc before 2.34 (BZ #18435): the flag stays "in progress" and every
//    later caller waits forever. run-binaries.sh runs this under a timeout.
//  - Mixing a static and the shared libgcc unwinder (-static-libgcc): the
//    process aborts with no message on the first throw.
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

int
main()
{
    std::once_flag flag;

    try
    {
        std::call_once(flag, [] { throw std::runtime_error("initializer failed"); });
        std::cerr << "ERROR: call_once did not propagate the exception\n";
        return 1;
    }
    catch (std::runtime_error const&)
    {
        // Flushed so it still shows when the next call hangs and is killed.
        std::cout << "First call_once threw as expected" << std::endl;
    }

    // Another thread is the case BZ #18435 deadlocks on.
    int calls = 0;
    std::thread([&] { std::call_once(flag, [&] { ++calls; }); }).join();
    std::call_once(flag, [&] { ++calls; });

    if (calls != 1)
    {
        std::cerr << "ERROR: expected the callable to run once after the throw, ran " << calls
                  << " times\n";
        return 1;
    }

    std::cout << "call_once recovered after exception\n";
    return 0;
}
