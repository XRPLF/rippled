#include <iostream>
#include <thread>

static int kCounter = 0;

void
increment()
{
    for (int i = 0; i < 100'000; ++i)
    {
        ++kCounter;
    }
}

int
main()
{
    std::thread t1(increment);
    std::thread t2(increment);

    t1.join();
    t2.join();

    std::cout << "Final counter value: " << kCounter << std::endl;
    return 0;
}
