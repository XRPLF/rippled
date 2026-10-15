#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

static std::mutex gMutex;

void
worker(int id)
{
    std::lock_guard<std::mutex> lock(gMutex);
    std::cout << "Hello from thread " << id << "\n";
}

int
main()
{
    constexpr int kNumThreads = 10;
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i)
        threads.emplace_back(worker, i);
    for (auto& t : threads)
        t.join();

    std::cout << "Hello from main thread\n";
    return 0;
}
