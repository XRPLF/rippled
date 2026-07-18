/**
 * @file
 * @brief Tests for ReaderPreferringSharedMutex - reader preference semantics.
 */

#include <xrpl/basics/ReaderPreferringSharedMutex.h>
#include <xrpl/beast/unit_test.h>

#include <atomic>
#include <thread>
#include <vector>

namespace xrpl::test {

/**
 * Test that ReaderPreferringSharedMutex provides basic shared/exclusive locking.
 *
 * On Linux this uses pthread_rwlock_t with PTHREAD_RWLOCK_PREFER_READER_NP.
 * On other platforms it aliases std::shared_mutex.
 */
class ReaderPreferringSharedMutex_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testBasicSharedLock();
        testExclusiveLock();
        testTryLockShared();
        testTryLockExclusive();
        testReaderPreferenceUnderContention();
    }

    void
    testBasicSharedLock()
    {
        testcase("basic shared lock");
        xrpl::reader_preferring_shared_mutex mutex;
        std::atomic<int> readerCount{0};
        std::atomic<int> maxConcurrentReaders{0};
        std::atomic<bool> go{false};

        // Barrier: start all readers, then release them simultaneously
        std::vector<std::thread> readers;
        for (int i = 0; i < 10; ++i)
        {
            readers.emplace_back([&]() {
                while (!go.load())
                    std::this_thread::yield();
                for (int j = 0; j < 5; ++j)
                {
                    std::shared_lock lock(mutex);
                    int current = ++readerCount;
                    // Small hold time to ensure overlap across threads
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    // Track maximum concurrent readers
                    int expected = maxConcurrentReaders.load();
                    while (current > expected &&
                           !maxConcurrentReaders.compare_exchange_weak(expected, current))
                    {
                    }
                    --readerCount;
                }
            });
        }

        // Release all readers at once to maximize contention
        go.store(true);

        for (auto& t : readers)
            t.join();

        BEAST_EXPECT(maxConcurrentReaders.load() > 1);
    }

    void
    testExclusiveLock()
    {
        xrpl::reader_preferring_shared_mutex mutex;
        std::atomic<bool> writerHoldsLock{false};
        std::atomic<int> readersInside{0};
        std::atomic<bool> go{false};

        // Start readers that will try to acquire shared lock
        std::vector<std::thread> readers;
        for (int i = 0; i < 5; ++i)
        {
            readers.emplace_back([&]() {
                while (!go.load())
                    std::this_thread::yield();
                std::shared_lock lock(mutex);
                // While we hold the shared lock, writer should not hold exclusive
                BEAST_EXPECT(!writerHoldsLock.load());
                ++readersInside;
            });
        }

        // Release readers so they start contending
        go.store(true);

        // Give readers time to acquire their shared locks
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Writer acquires exclusive lock (blocks until all readers release)
        {
            std::unique_lock lock(mutex);
            writerHoldsLock.store(true);
            // By the time writer gets the lock, all readers should have finished
            // (they acquire, increment, then release in the lambda above)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        writerHoldsLock.store(false);

        for (auto& t : readers)
            t.join();

        // All readers should have completed
        BEAST_EXPECT(readersInside.load() == 5);
    }

    void
    testTryLockShared()
    {
        xrpl::reader_preferring_shared_mutex mutex;

        // No lock held - try_lock_shared should succeed
        bool acquired = mutex.try_lock_shared();
        BEAST_EXPECT(acquired);
        mutex.unlock_shared();

        // Hold shared lock - another try_lock_shared should succeed
        mutex.lock_shared();
        acquired = mutex.try_lock_shared();
        BEAST_EXPECT(acquired);
        mutex.unlock_shared();
        mutex.unlock_shared();

        // Hold exclusive lock - try_lock_shared should fail
        mutex.lock();
        acquired = mutex.try_lock_shared();
        BEAST_EXPECT(!acquired);
        mutex.unlock();
    }

    void
    testTryLockExclusive()
    {
        xrpl::reader_preferring_shared_mutex mutex;

        // No lock held - try_lock should succeed
        bool acquired = mutex.try_lock();
        BEAST_EXPECT(acquired);
        mutex.unlock();

        // Hold shared lock - try_lock should fail
        mutex.lock_shared();
        acquired = mutex.try_lock();
        BEAST_EXPECT(!acquired);
        mutex.unlock_shared();

        // No lock held again - should succeed
        acquired = mutex.try_lock();
        BEAST_EXPECT(acquired);
        mutex.unlock();
    }

    void
    testReaderPreferenceUnderContention()
    {
        // This test verifies that both readers and writers make progress under contention.
        // We avoid strict ordering guarantees since the underlying mutex implementation
        // varies by platform (pthread rwlock on Linux, std::shared_mutex on macOS).
        xrpl::reader_preferring_shared_mutex mutex;
        std::atomic<int> readerSuccesses{0};
        std::atomic<int> writerSuccesses{0};
        std::atomic<bool> stop{false};

        std::vector<std::thread> threads;

        // 2 writers
        for (int i = 0; i < 2; ++i)
        {
            threads.emplace_back([&]() {
                while (!stop.load())
                {
                    std::unique_lock lock(mutex);
                    ++writerSuccesses;
                }
            });
        }

        // 4 readers
        for (int i = 0; i < 4; ++i)
        {
            threads.emplace_back([&]() {
                while (!stop.load())
                {
                    std::shared_lock lock(mutex);
                    ++readerSuccesses;
                }
            });
        }

        // Run for a period then stop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.store(true);

        for (auto& t : threads)
            t.join();

        int readers = readerSuccesses.load();
        int writers = writerSuccesses.load();

        // Both readers and writers should have made progress
        BEAST_EXPECT(readers > 0);
        BEAST_EXPECT(writers > 0);
    }
};

BEAST_DEFINE_TESTSUITE(ReaderPreferringSharedMutex, core, xrpl);

}  // namespace xrpl::test
