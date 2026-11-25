#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>
#include <xrpl/basics/SharedWeakCachePointer.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/Meter.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <latch>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// A clock that one thread can advance while others read it, which ManualClock
// does not allow.
class ConcurrencyTestClock : public beast::AbstractClock<std::chrono::steady_clock>
{
    std::atomic<rep> ticks_{0};

public:
    [[nodiscard]] time_point
    now() const override
    {
        return time_point(duration(ticks_.load()));
    }

    void
    advance(std::chrono::seconds amount)
    {
        ticks_.fetch_add(std::chrono::duration_cast<duration>(amount).count());
    }
};

// Calls `body` repeatedly on its own thread until stopped or destroyed. The
// destructor joins, so a failed assertion cannot leave the thread running.
class ConcurrencyTestLoop
{
    std::atomic<bool> stop_{false};
    std::thread thread_;

public:
    // The pause keeps the loop from starving the threads under test.
    template <class Body>
    explicit ConcurrencyTestLoop(
        Body body,
        std::chrono::milliseconds pause = std::chrono::milliseconds{1})
        : thread_([this, body = std::move(body), pause] mutable {
            while (!stop_)
            {
                body();
                if (pause.count() != 0)
                    std::this_thread::sleep_for(pause);
            }
        })
    {
    }

    ConcurrencyTestLoop(ConcurrencyTestLoop const&) = delete;
    ConcurrencyTestLoop&
    operator=(ConcurrencyTestLoop const&) = delete;

    ~ConcurrencyTestLoop()
    {
        stop();
    }

    void
    stop()
    {
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
    }
};

// The object type of the intrusive-pointer cache, which is what the SHAMap
// tree node cache uses.
struct ConcurrencyRefCountObject : IntrusiveRefCounts
{
    std::string data;

    // Needed to support weak intrusive pointers
    virtual void
    partialDestructor()
    {
    }

    ConcurrencyRefCountObject() = default;
    explicit ConcurrencyRefCountObject(std::string data) : data(std::move(data))
    {
    }
};

// A mutex that records how the cache uses it: how many times it is taken,
// whether a thread takes a second one while holding the first, and whether one
// is taken recursively. Those rules can then be checked deterministically. It
// cannot tell whether data is read after the lock is released: only
// ThreadSanitizer reports that.
class LockCheckingMutex
{
public:
    // Number of these mutexes the calling thread holds.
    static inline thread_local int held = 0;
    // Acquisitions made by a thread that already held a different mutex.
    static inline std::atomic<int> nested{0};
    // Acquisitions of a mutex the calling thread already held.
    static inline std::atomic<int> recursive{0};
    static inline std::atomic<std::uint64_t> acquisitions{0};
    // Number of different mutexes acquired at least once.
    static inline std::atomic<std::uint64_t> distinct{0};
    // Called, when set, just before an acquisition. It may use the cache: its
    // own acquisitions do not call it again.
    static inline std::function<void()> beforeLock;

    static void
    reset()
    {
        nested = 0;
        recursive = 0;
        acquisitions = 0;
        distinct = 0;
        beforeLock = nullptr;
    }

    void
    lock()
    {
        auto const self = std::this_thread::get_id();
        if (owner_.load() == self)
        {
            // Count it and carry on, so that a regression fails the test
            // instead of deadlocking it.
            ++recursive;
            ++depth_;
            return;
        }
        if (held != 0)
            ++nested;
        if (beforeLock && !inHook)
        {
            inHook = true;
            beforeLock();
            inHook = false;
        }
        mutex_.lock();
        owner_ = self;
        depth_ = 1;
        ++held;
        ++acquisitions;
        if (!seen_)
        {
            seen_ = true;
            ++distinct;
        }
    }

    void
    unlock()
    {
        if (--depth_ > 0)
            return;
        owner_ = std::thread::id{};
        --held;
        mutex_.unlock();
    }

private:
    static inline thread_local bool inHook = false;

    std::mutex mutex_;
    std::atomic<std::thread::id> owner_;
    int depth_{0};
    bool seen_{false};
};

template <class T, bool IsKeyCache = false>
using LockCheckingCache = TaggedCache<
    LedgerIndex,
    T,
    IsKeyCache,
    SharedWeakCachePointer<T>,
    std::shared_ptr<T>,
    HardenedHash<>,
    std::equal_to<LedgerIndex>,
    LockCheckingMutex>;

// Counts the times getKeys() had to enlarge its buffer after the first
// allocation.
class RetryCountingSink : public beast::Journal::Sink
{
public:
    std::atomic<int> retries{0};

    RetryCountingSink() : Sink(beast::Severity::Info, false)
    {
    }

    void
    write(beast::Severity, std::string const& text) override
    {
        if (text.find("Retrying allocation") != std::string::npos)
            ++retries;
    }

    void
    writeAlways(beast::Severity level, std::string const& text) override
    {
        write(level, text);
    }
};

// A collector whose hook the test fires itself, the way a real collector's
// timer thread does, and whose gauges remember the last value set.
class ManualCollector : public beast::insight::Collector
{
    struct StoredHook : beast::insight::HookImpl
    {
        HandlerType handler;

        explicit StoredHook(HandlerType handler) : handler(std::move(handler))
        {
        }
    };

    struct RecordingGauge : beast::insight::GaugeImpl
    {
        std::atomic<value_type> value{0};

        void
        set(value_type newValue) override
        {
            value = newValue;
        }

        void
        increment(difference_type amount) override
        {
            value.fetch_add(static_cast<value_type>(amount));
        }
    };

    // Weak, so that the test can tell when the cache lets go of its hook.
    std::weak_ptr<StoredHook> hook_;
    std::map<std::string, std::shared_ptr<RecordingGauge>> gauges_;

public:
    beast::insight::Hook
    makeHook(beast::insight::HookImpl::HandlerType const& handler) override
    {
        auto hook = std::make_shared<StoredHook>(handler);
        hook_ = hook;
        return beast::insight::Hook{std::move(hook)};
    }

    beast::insight::Counter
    makeCounter(std::string const&) override
    {
        return beast::insight::Counter{};
    }

    beast::insight::Event
    makeEvent(std::string const&) override
    {
        return beast::insight::Event{};
    }

    beast::insight::Gauge
    makeGauge(std::string const& name) override
    {
        auto gauge = std::make_shared<RecordingGauge>();
        gauges_[name] = gauge;
        return beast::insight::Gauge{gauge};
    }

    beast::insight::Meter
    makeMeter(std::string const&) override
    {
        return beast::insight::Meter{};
    }

    void
    collect() const
    {
        if (auto const hook = hook_.lock())
            hook->handler();
    }

    [[nodiscard]] bool
    hookRegistered() const
    {
        return !hook_.expired();
    }

    [[nodiscard]] std::uint64_t
    gauge(std::string const& name) const
    {
        return gauges_.at(name)->value;
    }
};

// A cached object that records, when it is destroyed, whether the collector
// still had the hook of the cache that held it.
struct HookProbe
{
    ManualCollector const* collector = nullptr;
    bool* hookRegisteredAtDestruction = nullptr;

    ~HookProbe()
    {
        if (collector != nullptr)
            *hookRegisteredAtDestruction = collector->hookRegistered();
    }
};

// A key that counts every use of it (hash, compare, copy, destroy) made, while
// armed, by a thread that does not hold exactly one partition lock.
struct LockCheckedKey
{
    static inline std::atomic<bool> armed{false};
    static inline std::atomic<int> unguarded{0};

    std::uint32_t value{0};

    static void
    check()
    {
        if (armed && LockCheckingMutex::held != 1)
            ++unguarded;
    }

    explicit LockCheckedKey(std::uint32_t value) : value(value)
    {
    }

    LockCheckedKey(LockCheckedKey const& other) : value(other.value)
    {
        check();
    }

    LockCheckedKey&
    operator=(LockCheckedKey const&) = default;

    ~LockCheckedKey()
    {
        check();
    }

    friend bool
    operator==(LockCheckedKey const& lhs, LockCheckedKey const& rhs)
    {
        check();
        return lhs.value == rhs.value;
    }
};

// Picks the partition. The cache finds it by argument-dependent lookup.
[[maybe_unused]] std::size_t
extract(LockCheckedKey const& key)
{
    return key.value;
}

struct LockCheckedKeyHash
{
    std::size_t
    operator()(LockCheckedKey const& key) const
    {
        LockCheckedKey::check();
        return std::hash<std::uint32_t>{}(key.value);
    }
};

template <class T, bool IsKeyCache = false>
using LockCheckedKeyCache = TaggedCache<
    LockCheckedKey,
    T,
    IsKeyCache,
    SharedWeakCachePointer<T>,
    std::shared_ptr<T>,
    LockCheckedKeyHash,
    std::equal_to<LockCheckedKey>,
    LockCheckingMutex>;

}  // namespace

struct TaggedCacheConcurrencyTest : public ::testing::Test
{
    static constexpr unsigned kThreads = 8;
    static constexpr unsigned kIterations = 20000;

    beast::Journal const journal{beast::Journal::getNullSink()};
    ConcurrencyTestClock clock;

    // Runs `body(threadIndex)` on `count` threads that all start together.
    template <class Body>
    static void
    runThreads(unsigned count, Body&& body)
    {
        std::latch start{1};
        std::vector<std::thread> threads;
        threads.reserve(count);
        for (unsigned t = 0; t < count; ++t)
        {
            threads.emplace_back([&, t] {
                start.wait();
                body(t);
            });
        }
        start.count_down();
        for (auto& thread : threads)
            thread.join();
    }

    void
    SetUp() override
    {
        LockCheckingMutex::reset();
    }
};

// Every single-key operation at once on a value cache, while other threads
// sweep and read the totals. In the first half a further thread clears and
// resets the cache; the second half runs without it, because clear() zeroes
// the counts and would hide a miscount. A removal that is counted twice or not
// at all in the second half is caught by the last assertions: once everything
// has expired and nothing is held, the cache must be exactly empty.
TEST_F(TaggedCacheConcurrencyTest, mixed_operations_leave_counts_consistent)
{
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"stress", 64, std::chrono::seconds{2}, clock, journal};

    {
        ConcurrencyTestLoop const sweeper{[&] {
            clock.advance(std::chrono::seconds{1});
            cache.sweep();
        }};
        ConcurrencyTestLoop const reader{[&] {
            std::uint64_t total = 0;
            total += cache.size();
            total += cache.getTrackSize();
            total += cache.getCacheSize();
            total += cache.getKeys().size();
            total += static_cast<std::uint64_t>(cache.getHitRate());
            total += static_cast<std::uint64_t>(cache.rate());
            (void)total;
        }};
        ConcurrencyTestLoop clearer{
            [&, round = 0] mutable {
                if (++round % 2 == 0)
                {
                    cache.clear();
                }
                else
                {
                    cache.reset();
                }
            },
            std::chrono::milliseconds{2}};

        auto const work = [&](unsigned t) {
            std::mt19937 rng{t};
            std::vector<std::shared_ptr<std::string>> held;
            for (unsigned i = 0; i < kIterations / 2; ++i)
            {
                LedgerIndex const key = rng() % 512;
                switch (rng() % 8)
                {
                    case 0:
                        cache.insert(key, "v");
                        break;
                    case 1:
                        if (auto p = cache.fetch(key); p && (rng() % 4 == 0))
                            held.push_back(std::move(p));
                        break;
                    case 2: {
                        auto p = std::make_shared<std::string>("c");
                        cache.canonicalizeReplaceClient(key, p);
                        break;
                    }
                    case 3:
                        cache.canonicalizeReplaceCache(key, std::make_shared<std::string>("r"));
                        break;
                    case 4:
                        cache.del(key, rng() % 2 == 0);
                        break;
                    case 5:
                        // Read only: other threads read these values through
                        // fetch() with no lock, which the class allows only
                        // for objects nobody modifies.
                        cache.fetchAndModify(key, [](std::string const& s) { (void)s.size(); });
                        break;
                    case 6:
                        cache.touchIfExists(key);
                        break;
                    default: {
                        std::string out;
                        cache.retrieve(key, out);
                        break;
                    }
                }
                if (held.size() > 32)
                    held.clear();
            }
        };
        runThreads(kThreads, work);

        // clear() zeroes the counts, which hides a miscount made before it.
        // Run the workload again without it, so that a miscount made now
        // reaches the assertions below.
        clearer.stop();
        runThreads(kThreads, work);
    }

    EXPECT_GE(cache.getCacheSize(), 0);
    EXPECT_LE(cache.getCacheSize(), cache.getTrackSize());

    // Nothing is held any more. Age everything out.
    clock.advance(std::chrono::seconds{10});
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
    EXPECT_EQ(cache.size(), 0u);
}

// The same on the intrusive-pointer instantiation, whose strong-to-weak
// conversion and use counts are different code.
TEST_F(TaggedCacheConcurrencyTest, intrusive_mixed_operations_leave_counts_consistent)
{
    using Object = ConcurrencyRefCountObject;
    using Cache = TaggedCache<
        LedgerIndex,
        Object,
        /*IsKeyCache*/ false,
        intr_ptr::SharedWeakUnionPtr<Object>,
        intr_ptr::SharedPtr<Object>>;
    Cache cache{"intrusive", 64, std::chrono::seconds{2}, clock, journal};

    {
        ConcurrencyTestLoop const sweeper{[&] {
            clock.advance(std::chrono::seconds{1});
            cache.sweep();
        }};
        ConcurrencyTestLoop clearer{
            [&] {
                cache.clear();
                (void)cache.getKeys();
                (void)cache.getCacheSize();
            },
            std::chrono::milliseconds{2}};

        auto const work = [&](unsigned t) {
            std::mt19937 rng{t};
            std::vector<intr_ptr::SharedPtr<Object>> held;
            for (unsigned i = 0; i < kIterations / 2; ++i)
            {
                LedgerIndex const key = rng() % 512;
                switch (rng() % 5)
                {
                    case 0:
                        cache.canonicalizeReplaceCache(key, intr_ptr::makeShared<Object>("r"));
                        break;
                    case 1: {
                        auto p = intr_ptr::makeShared<Object>("c");
                        cache.canonicalizeReplaceClient(key, p);
                        if (rng() % 4 == 0)
                            held.push_back(std::move(p));
                        break;
                    }
                    case 2:
                        if (auto p = cache.fetch(key); p && (rng() % 4 == 0))
                            held.push_back(std::move(p));
                        break;
                    case 3:
                        cache.del(key, rng() % 2 == 0);
                        break;
                    default:
                        cache.fetchAndModify(key, [](Object const& o) { (void)o.data.size(); });
                        break;
                }
                if (held.size() > 32)
                    held.clear();
            }
        };
        runThreads(kThreads, work);

        // clear() zeroes the counts, which hides a miscount made before it.
        // Run the workload again without it, so that a miscount made now
        // reaches the assertions below.
        clearer.stop();
        runThreads(kThreads, work);
    }

    EXPECT_GE(cache.getCacheSize(), 0);
    EXPECT_LE(cache.getCacheSize(), cache.getTrackSize());

    clock.advance(std::chrono::seconds{10});
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
}

// A key-only cache counts every key it tracks, so its two sizes must agree
// whenever the cache is quiet, whatever ran concurrently before.
TEST_F(TaggedCacheConcurrencyTest, key_cache_sizes_agree_after_concurrent_use)
{
    using Cache = TaggedCache<UInt256, int, true>;
    Cache cache{"keys", 64, std::chrono::seconds{2}, clock, journal};

    {
        ConcurrencyTestLoop const sweeper{[&] {
            clock.advance(std::chrono::seconds{1});
            cache.sweep();
        }};
        ConcurrencyTestLoop clearer{[&] { cache.clear(); }, std::chrono::milliseconds{2}};

        auto const work = [&](unsigned t) {
            std::mt19937_64 rng{t};
            for (unsigned i = 0; i < kIterations / 2; ++i)
            {
                // Vary the leading bytes: they pick the partition.
                UInt256 key;
                auto const r = rng() % 2048;
                std::memcpy(key.data(), &r, sizeof(r));
                if (rng() % 2 == 0)
                {
                    cache.insert(key);
                }
                else
                {
                    cache.touchIfExists(key);
                }
            }
        };
        runThreads(kThreads, work);

        // clear() zeroes the counts, which hides a miscount made before it.
        // Run the workload again without it, so that a miscount made now
        // reaches the assertions below.
        clearer.stop();
        runThreads(kThreads, work);
    }

    EXPECT_EQ(cache.getCacheSize(), cache.getTrackSize());
    EXPECT_EQ(static_cast<std::size_t>(cache.getTrackSize()), cache.size());

    clock.advance(std::chrono::seconds{10});
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
}

// Hits and misses are counted per partition and summed. With a known number
// of each, from many threads, a single lost update changes the rate.
TEST_F(TaggedCacheConcurrencyTest, hits_and_misses_are_counted_exactly)
{
    constexpr LedgerIndex kKeys = 256;

    {
        using Cache = TaggedCache<LedgerIndex, std::string>;
        Cache cache{"rate", 0, std::chrono::hours{1}, clock, journal};
        for (LedgerIndex key = 0; key < kKeys; ++key)
            cache.insert(key, "v");

        runThreads(kThreads, [&](unsigned t) {
            for (unsigned i = 0; i < kIterations; ++i)
            {
                LedgerIndex const key = (t + i) % kKeys;
                // Three hits for every miss.
                cache.fetch(key);
                cache.touchIfExists(key);
                std::string out;
                cache.retrieve(key, out);
                cache.fetch(key + kKeys);
            }
        });

        EXPECT_DOUBLE_EQ(cache.rate(), 0.75);
        EXPECT_NEAR(cache.getHitRate(), 75.0f, 0.01f);
        EXPECT_EQ(cache.getCacheSize(), static_cast<int>(kKeys));
    }

    {
        using Cache = TaggedCache<LedgerIndex, int, true>;
        Cache cache{"keyrate", 0, std::chrono::hours{1}, clock, journal};
        for (LedgerIndex key = 0; key < kKeys; ++key)
            cache.insert(key);

        runThreads(kThreads, [&](unsigned t) {
            for (unsigned i = 0; i < kIterations; ++i)
            {
                LedgerIndex const key = (t + i) % kKeys;
                cache.touchIfExists(key);
                cache.touchIfExists(key + kKeys);
            }
        });

        EXPECT_DOUBLE_EQ(cache.rate(), 0.5);
        EXPECT_EQ(cache.getCacheSize(), static_cast<int>(kKeys));
    }
}

// The callback of fetchAndModify() is the only protection the cached object
// has. A plain int is incremented from many threads while a sweep scans the
// entries: if two callbacks for one key ever overlap, increments are lost.
TEST_F(TaggedCacheConcurrencyTest, fetch_and_modify_callback_is_exclusive_per_key)
{
    struct Counter
    {
        int value = 0;
    };

    using Cache = TaggedCache<LedgerIndex, Counter>;
    // Long expiry: sweeps scan every partition but must not drop a counter.
    Cache cache{"exclusive", 0, std::chrono::hours{1}, clock, journal};

    constexpr unsigned kKeys = 16;
    {
        ConcurrencyTestLoop const sweeper{[&] { cache.sweep(); }};

        runThreads(kThreads, [&](unsigned t) {
            for (unsigned i = 0; i < kIterations; ++i)
                cache.fetchAndModify((t + i) % kKeys, [](Counter& counter) { ++counter.value; });
        });
    }

    int total = 0;
    for (LedgerIndex key = 0; key < kKeys; ++key)
    {
        auto const counter = cache.fetch(key);
        ASSERT_NE(counter, nullptr);
        total += counter->value;
    }
    EXPECT_EQ(total, static_cast<int>(kThreads * kIterations));
}

// getKeys() copies one partition at a time. Whatever changes meanwhile, it
// must not report a key twice or lose a key that was present throughout.
TEST_F(TaggedCacheConcurrencyTest, get_keys_returns_each_stable_key_once)
{
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"keys", 0, std::chrono::hours{1}, clock, journal};

    constexpr LedgerIndex kStable = 1000;
    for (LedgerIndex key = 0; key < kStable; ++key)
        cache.insert(key, "s");

    bool duplicated = false;
    bool lost = false;
    {
        // A bounded range of other keys comes and goes.
        ConcurrencyTestLoop const writer{
            [&, i = LedgerIndex{0}] mutable {
                constexpr LedgerIndex kChurn = 4096;
                for (unsigned batch = 0; batch < 64; ++batch, ++i)
                {
                    LedgerIndex const key = kStable + (i % kChurn);
                    cache.insert(key, "w");
                    if (i % 3 != 0)
                        cache.del(key, false);
                }
            },
            std::chrono::milliseconds{0}};

        for (int round = 0; round < 200 && !duplicated && !lost; ++round)
        {
            auto keys = cache.getKeys();
            std::ranges::sort(keys);
            duplicated = std::ranges::adjacent_find(keys) != keys.end();
            // The stable keys are the smallest, so they lead the sorted list.
            lost = keys.size() < kStable;
            for (LedgerIndex key = 0; key < kStable && !lost; ++key)
                lost = keys[key] != key;
        }
    }

    EXPECT_FALSE(duplicated);
    EXPECT_FALSE(lost);
}

// fetch(key, handler) under load. No counts are asserted: on this path the
// insert is not counted, which is a known issue that predates the partition
// locks. The value of the test is in running under a sanitizer.
TEST_F(TaggedCacheConcurrencyTest, fetch_with_handler_survives_concurrent_sweep)
{
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"handler", 64, std::chrono::seconds{2}, clock, journal};

    {
        ConcurrencyTestLoop const sweeper{[&] {
            clock.advance(std::chrono::seconds{1});
            cache.sweep();
        }};

        runThreads(kThreads, [&](unsigned t) {
            std::mt19937 rng{t};
            for (unsigned i = 0; i < kIterations; ++i)
            {
                LedgerIndex const key = rng() % 512;
                if (rng() % 4 == 0)
                {
                    cache.del(key, rng() % 2 == 0);
                }
                else
                {
                    cache.fetch(key, [] { return std::make_shared<std::string>("h"); });
                }
            }
        });
    }

    EXPECT_LE(cache.getCacheSize(), cache.getTrackSize());
}

// Sizes, counters and keys are sums over every partition.
TEST_F(TaggedCacheConcurrencyTest, counters_and_keys_add_up_across_partitions)
{
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"sum", 0, std::chrono::hours{1}, clock, journal};

    // Consecutive keys land in consecutive partitions.
    constexpr LedgerIndex kKeys = 512;
    for (LedgerIndex key = 0; key < kKeys; ++key)
        EXPECT_FALSE(cache.insert(key, "v"));

    EXPECT_EQ(cache.size(), kKeys);
    EXPECT_EQ(cache.getTrackSize(), static_cast<int>(kKeys));
    EXPECT_EQ(cache.getCacheSize(), static_cast<int>(kKeys));

    auto keys = cache.getKeys();
    std::ranges::sort(keys);
    ASSERT_EQ(keys.size(), kKeys);
    for (LedgerIndex key = 0; key < kKeys; ++key)
        EXPECT_EQ(keys[key], key);

    // One hit and one miss in every partition.
    for (LedgerIndex key = 0; key < kKeys; ++key)
    {
        EXPECT_NE(cache.fetch(key), nullptr);
        EXPECT_EQ(cache.fetch(key + kKeys), nullptr);
    }
    EXPECT_DOUBLE_EQ(cache.rate(), 0.5);
    EXPECT_FLOAT_EQ(cache.getHitRate(), 50.0f);

    // clear() empties the cache but keeps the hit rate; reset() zeroes both.
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_DOUBLE_EQ(cache.rate(), 0.5);

    cache.reset();
    EXPECT_DOUBLE_EQ(cache.rate(), 0.0);
}

// The locking rules, checked through an instrumented mutex: a single-key
// operation takes one lock, an operation on the whole cache takes one lock
// per partition and never two at once, callbacks run under exactly one lock,
// the fetch handler runs under none, and nothing locks recursively.
TEST_F(TaggedCacheConcurrencyTest, each_operation_takes_one_partition_lock_at_a_time)
{
    LockCheckingCache<std::string> cache{"checked", 1, std::chrono::seconds{1}, clock, journal};
    auto const taken = [] { return LockCheckingMutex::acquisitions.exchange(0); };

    for (LedgerIndex key = 0; key < 300; ++key)
        cache.insert(key, "v");
    EXPECT_EQ(taken(), 300u);

    EXPECT_NE(cache.fetch(1), nullptr);
    EXPECT_EQ(taken(), 1u);
    EXPECT_EQ(cache.fetch(100000), nullptr);
    EXPECT_EQ(taken(), 1u);
    EXPECT_TRUE(cache.touchIfExists(2));
    EXPECT_EQ(taken(), 1u);
    EXPECT_TRUE(cache.del(3, false));
    EXPECT_EQ(taken(), 1u);
    std::string out;
    EXPECT_TRUE(cache.retrieve(4, out));
    EXPECT_EQ(taken(), 1u);

    auto p = std::make_shared<std::string>("x");
    bool called = false;
    EXPECT_TRUE(cache.canonicalize(5, p, [&](auto const&) {
        called = true;
        EXPECT_EQ(LockCheckingMutex::held, 1);
        return false;
    }));
    EXPECT_TRUE(called);
    EXPECT_EQ(taken(), 1u);
    cache.canonicalizeReplaceCache(5, p);
    cache.canonicalizeReplaceClient(5, p);
    EXPECT_EQ(taken(), 2u);

    called = false;
    cache.fetchAndModify(6, [&](std::string&) {
        called = true;
        EXPECT_EQ(LockCheckingMutex::held, 1);
    });
    EXPECT_TRUE(called);
    EXPECT_EQ(taken(), 1u);

    // The handler runs between two lock scopes, so it may use the cache, even
    // for the same key.
    called = false;
    auto const fetched = cache.fetch(700, [&] {
        called = true;
        EXPECT_EQ(LockCheckingMutex::held, 0);
        EXPECT_EQ(cache.fetch(700), nullptr);
        return std::make_shared<std::string>("h");
    });
    EXPECT_TRUE(called);
    EXPECT_NE(fetched, nullptr);
    EXPECT_EQ(taken(), 3u);

    // One lock per partition, however many partitions this machine gets.
    EXPECT_GT(cache.size(), 0u);
    auto const partitions = taken();
    ASSERT_GE(partitions, 1u);
    EXPECT_GT(cache.getTrackSize(), 0);
    EXPECT_EQ(taken(), partitions);
    EXPECT_GT(cache.getCacheSize(), 0);
    EXPECT_EQ(taken(), partitions);
    EXPECT_GT(cache.getHitRate(), 0.0f);
    EXPECT_EQ(taken(), partitions);
    EXPECT_GT(cache.rate(), 0.0);
    EXPECT_EQ(taken(), partitions);
    // One pass to size the buffer and one to copy.
    EXPECT_FALSE(cache.getKeys().empty());
    EXPECT_EQ(taken(), 2 * partitions);
    // One pass to pick the cut-off and one lock per worker.
    clock.advance(std::chrono::seconds{2});
    cache.sweep();
    EXPECT_EQ(taken(), 2 * partitions);
    cache.clear();
    EXPECT_EQ(taken(), partitions);
    cache.reset();
    EXPECT_EQ(taken(), partitions);

    LockCheckingCache<int, true> keys{"checkedkeys", 1, std::chrono::seconds{1}, clock, journal};
    EXPECT_TRUE(keys.insert(1));
    EXPECT_TRUE(keys.touchIfExists(1));
    EXPECT_EQ(taken(), 2u);
    clock.advance(std::chrono::seconds{2});
    keys.sweep();
    EXPECT_EQ(taken(), 2 * partitions);
    EXPECT_EQ(keys.size(), 0u);

    EXPECT_EQ(LockCheckingMutex::nested.load(), 0);
    EXPECT_EQ(LockCheckingMutex::recursive.load(), 0);
    EXPECT_EQ(LockCheckingMutex::held, 0);
}

// getKeys() sizes its buffer with one pass over the partitions and copies
// them with a second. If the cache outgrows the buffer in between, a single
// reallocation must cover the rest of the copy.
TEST_F(TaggedCacheConcurrencyTest, get_keys_reallocates_once_when_the_cache_grows)
{
    RetryCountingSink sink;
    beast::Journal const countingJournal{sink};
    LockCheckingCache<std::string> cache{"grow", 0, std::chrono::hours{1}, clock, countingJournal};

    constexpr LedgerIndex kBefore = 100;
    constexpr LedgerIndex kAfter = 1000;
    for (LedgerIndex key = 0; key < kBefore; ++key)
        cache.insert(key, "early");

    LockCheckingMutex::acquisitions = 0;
    EXPECT_EQ(cache.size(), kBefore);
    auto const partitions = LockCheckingMutex::acquisitions.load();

    // Grow the cache tenfold just before the first lock of the copying pass.
    std::uint64_t locks = 0;
    LockCheckingMutex::beforeLock = [&] {
        if (++locks == partitions + 1)
        {
            for (LedgerIndex key = kBefore; key < kAfter; ++key)
                cache.insert(key, "late");
        }
    };
    auto keys = cache.getKeys();
    LockCheckingMutex::beforeLock = nullptr;

    std::ranges::sort(keys);
    ASSERT_EQ(keys.size(), kAfter);
    for (LedgerIndex key = 0; key < kAfter; ++key)
        EXPECT_EQ(keys[key], key);

    EXPECT_EQ(sink.retries.load(), 1);
    EXPECT_EQ(LockCheckingMutex::nested.load(), 0);
    EXPECT_EQ(LockCheckingMutex::recursive.load(), 0);
}

// The insight hook reads the counters from the collector's thread while the
// cache is in use, and reports the totals of all partitions.
TEST_F(TaggedCacheConcurrencyTest, metrics_hook_reports_the_totals)
{
    auto const collector = std::make_shared<ManualCollector>();
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"metrics", 0, std::chrono::hours{1}, clock, journal, collector};

    {
        ConcurrencyTestLoop const timer{
            [&] { collector->collect(); }, std::chrono::milliseconds{0}};

        runThreads(kThreads, [&](unsigned t) {
            std::mt19937 rng{t};
            for (unsigned i = 0; i < kIterations; ++i)
            {
                LedgerIndex const key = rng() % 512;
                if (rng() % 2 == 0)
                {
                    cache.insert(key, "v");
                }
                else
                {
                    cache.fetch(key);
                }
            }
        });
    }

    cache.reset();
    constexpr LedgerIndex kKeys = 64;
    for (LedgerIndex key = 0; key < kKeys; ++key)
        cache.insert(key, "v");
    // Three hits for every miss.
    for (LedgerIndex key = 0; key < kKeys; ++key)
    {
        cache.fetch(key);
        cache.fetch(key);
        cache.fetch(key);
        cache.fetch(key + kKeys);
    }

    collector->collect();
    EXPECT_EQ(collector->gauge("metrics.size"), kKeys);
    EXPECT_EQ(collector->gauge("metrics.hit_rate"), 75u);
}

// The cache counts an entry only while it holds a strong pointer to it. No
// other test reaches these two branches without a concurrent clear().
TEST_F(TaggedCacheConcurrencyTest, weak_entry_is_counted_only_while_cached)
{
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"weak", 0, std::chrono::seconds{1}, clock, journal};

    auto held = std::make_shared<std::string>("held");
    EXPECT_FALSE(cache.canonicalizeReplaceClient(1, held));
    clock.advance(std::chrono::seconds{2});
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 1);

    // del() of an entry that is only weakly held must not change the count.
    EXPECT_FALSE(cache.del(1, true));
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 1);

    // fetch() makes it strong again and counts it.
    EXPECT_EQ(cache.fetch(1), held);
    EXPECT_EQ(cache.getCacheSize(), 1);
    EXPECT_EQ(cache.getTrackSize(), 1);
}

// fetch(key, handler) runs the handler unlocked. If the key is inserted
// meanwhile, the caller must get the object that is in the cache.
TEST_F(TaggedCacheConcurrencyTest, fetch_with_handler_keeps_the_entry_inserted_meanwhile)
{
    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache cache{"race", 0, std::chrono::hours{1}, clock, journal};

    std::shared_ptr<std::string> winner;
    auto const loser = cache.fetch(7, [&] {
        winner = cache.fetch(7, [] { return std::make_shared<std::string>("winner"); });
        return std::make_shared<std::string>("loser");
    });
    ASSERT_NE(winner, nullptr);
    EXPECT_EQ(loser, winner);
    EXPECT_EQ(cache.fetch(7), winner);
    EXPECT_EQ(cache.size(), 1u);
}

// There is one partition per hardware thread, and consecutive keys use all of
// them. Everything else passes with a single partition, which would silently
// undo the point of having partitions.
TEST_F(TaggedCacheConcurrencyTest, keys_are_spread_over_one_partition_per_hardware_thread)
{
    auto const expected = std::max<std::uint64_t>(1, std::thread::hardware_concurrency());
    LockCheckingCache<std::string> cache{"spread", 0, std::chrono::hours{1}, clock, journal};

    for (LedgerIndex key = 0; key < expected; ++key)
        cache.insert(key, "v");
    EXPECT_EQ(LockCheckingMutex::distinct.load(), expected);

    LockCheckingMutex::acquisitions = 0;
    EXPECT_EQ(cache.size(), expected);
    EXPECT_EQ(LockCheckingMutex::acquisitions.load(), expected);
}

// The metrics hook reads the partitions from another thread, so the cache
// must let go of it before the partitions are destroyed.
TEST_F(TaggedCacheConcurrencyTest, hook_is_released_before_the_partitions_are_destroyed)
{
    auto const collector = std::make_shared<ManualCollector>();
    bool hookRegisteredAtDestruction = true;
    {
        TaggedCache<LedgerIndex, HookProbe> cache{
            "order", 0, std::chrono::hours{1}, clock, journal, collector};
        auto probe = std::make_shared<HookProbe>();
        probe->collector = collector.get();
        probe->hookRegisteredAtDestruction = &hookRegisteredAtDestruction;
        cache.canonicalizeReplaceCache(1, probe);
        probe.reset();
        EXPECT_TRUE(collector->hookRegistered());
    }
    EXPECT_FALSE(hookRegisteredAtDestruction);
}

// The instrumented mutex counts acquisitions but cannot see a map being used
// after its lock was released. These keys can: the map hashes, compares,
// copies and destroys them, and each such use must happen under exactly one
// partition lock.
TEST_F(TaggedCacheConcurrencyTest, map_is_only_touched_under_its_partition_lock)
{
    using Key = LockCheckedKey;
    std::vector<Key> keys;
    keys.reserve(64);
    for (std::uint32_t i = 0; i < 64; ++i)
        keys.emplace_back(i);

    auto const guarded = [](auto&& operation) {
        Key::unguarded = 0;
        Key::armed = true;
        operation();
        Key::armed = false;
        return Key::unguarded.load() == 0;
    };

    {
        LockCheckedKeyCache<std::string> cache{"guard", 1, std::chrono::seconds{1}, clock, journal};
        auto const fill = [&] {
            for (auto const& key : keys)
                cache.insert(key, "v");
        };
        EXPECT_TRUE(guarded(fill)) << "insert";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.fetch(key);
        })) << "fetch";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.touchIfExists(key);
        })) << "touchIfExists";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
            {
                std::string out;
                cache.retrieve(key, out);
            }
        })) << "retrieve";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.fetchAndModify(key, [](std::string&) {});
        })) << "fetchAndModify";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.canonicalizeReplaceCache(key, std::make_shared<std::string>("r"));
        })) << "canonicalizeReplaceCache";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.fetch(key, [] { return std::make_shared<std::string>("h"); });
        })) << "fetch with handler, found";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.del(key, false);
        })) << "del";
        EXPECT_EQ(cache.size(), 0u);
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.fetch(key, [] { return std::make_shared<std::string>("h"); });
        })) << "fetch with handler, inserted";
        cache.clear();
        fill();
        clock.advance(std::chrono::seconds{2});
        EXPECT_TRUE(guarded([&] { cache.sweep(); })) << "sweep";
        EXPECT_EQ(cache.size(), 0u);
        fill();
        EXPECT_TRUE(guarded([&] { cache.clear(); })) << "clear";
        fill();
        EXPECT_TRUE(guarded([&] { cache.reset(); })) << "reset";
    }
    {
        LockCheckedKeyCache<int, true> cache{
            "guardkeys", 1, std::chrono::seconds{1}, clock, journal};
        auto const fill = [&] {
            for (auto const& key : keys)
                cache.insert(key);
        };
        EXPECT_TRUE(guarded(fill)) << "key insert";
        EXPECT_TRUE(guarded([&] {
            for (auto const& key : keys)
                cache.touchIfExists(key);
        })) << "key touchIfExists";
        clock.advance(std::chrono::seconds{2});
        EXPECT_TRUE(guarded([&] { cache.sweep(); })) << "key sweep";
        EXPECT_EQ(cache.size(), 0u);
        fill();
        EXPECT_TRUE(guarded([&] { cache.clear(); })) << "key clear";
    }
}

}  // namespace xrpl
