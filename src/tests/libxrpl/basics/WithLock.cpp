#include <xrpl/basics/WithLock.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <latch>
#include <memory>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace xrpl;

namespace {

// Implement bare-minimum BasicLockable, whose operations are noexcept, to
// exercise the conditional noexcept specification and to observe guard
// behavior from outside.
struct NoexceptLock
{
    int lockCount = 0;
    int unlockCount = 0;
    bool locked = false;

    void
    lock() noexcept
    {
        ++lockCount;
        locked = true;
    }

    void
    unlock() noexcept
    {
        ++unlockCount;
        locked = false;
    }
};

// Type with member functions, for the std::invoke paths.
struct Counter
{
    int value = 0;

    int
    add(int n)
    {
        value += n;
        return value;
    }

    Counter&
    self()
    {
        return *this;
    }
};

//------------------------------------------------------------------------------
//
// Compile-time contract
//
// Everything in this block is checked by the compiler; the TEST cases below
// cover runtime behaviour only.
//
//------------------------------------------------------------------------------

template <class Lock, class F, class... Args>
concept WithLockAccepts = requires(Lock& l, F f, Args... args) { withLock(l, f, args...); };

template <class Lock, class F, class... Args>
concept WithLockRefAccepts = requires(Lock& l, F f, Args... args) { withLockRef(l, f, args...); };

// Callable shapes used in the assertions below.
struct ByValue
{
    int
    operator()() const
    {
        return 1;
    }
};

struct ByVoid
{
    void
    operator()() const
    {
    }
};

struct ByRef
{
    inline static int referent = 0;

    int&
    operator()() const
    {
        return referent;
    }
};

struct Nothrow
{
    int
    operator()() const noexcept
    {
        return 1;
    }
};

// Overload partition: withLock() takes value- and void-returning callables;
// withLockRef() takes reference-returning callables; neither takes the
// other's.
static_assert(WithLockAccepts<std::mutex, ByValue>);
static_assert(WithLockAccepts<std::mutex, ByVoid>);
static_assert(!WithLockAccepts<std::mutex, ByRef>);
static_assert(WithLockRefAccepts<std::mutex, ByRef>);
static_assert(!WithLockRefAccepts<std::mutex, ByValue>);
static_assert(!WithLockRefAccepts<std::mutex, ByVoid>);

// Result types are forwarded faithfully.
static_assert(
    std::is_same_v<decltype(withLock(std::declval<std::mutex&>(), std::declval<ByValue&>())), int>);
static_assert(
    std::is_void_v<decltype(withLock(std::declval<std::mutex&>(), std::declval<ByVoid&>()))>);
static_assert(std::is_same_v<
              decltype(withLockRef(std::declval<std::mutex&>(), std::declval<ByRef&>())),
              int&>);

// Lock types: anything BasicLockable is accepted, including shared and
// recursive mutexes (taken exclusively) and user-defined locks. Non-lockable
// types and const lockables are rejected.
static_assert(WithLockAccepts<std::recursive_mutex, ByValue>);
static_assert(WithLockAccepts<std::shared_mutex, ByValue>);
static_assert(WithLockAccepts<NoexceptLock, ByValue>);
static_assert(!WithLockAccepts<int, ByValue>);
static_assert(!WithLockAccepts<std::mutex const, ByValue>);

// Conditional noexcept: true only when both lock() and the invocation are
// noexcept. std::mutex::lock() is not declared noexcept, so withLock() over
// a std::mutex never is.
static_assert(noexcept(withLock(std::declval<NoexceptLock&>(), std::declval<Nothrow&>())));
static_assert(!noexcept(withLock(std::declval<NoexceptLock&>(), std::declval<ByValue&>())));
static_assert(!noexcept(withLock(std::declval<std::mutex&>(), std::declval<Nothrow&>())));

}  // namespace

//------------------------------------------------------------------------------
//
// Runtime behaviour
//
//------------------------------------------------------------------------------

TEST(WithLock, lock_held_during_invocation)
{
    NoexceptLock lock;
    bool heldInside = false;
    withLock(lock, [&] { heldInside = lock.locked; });
    EXPECT_TRUE(heldInside);
    EXPECT_FALSE(lock.locked);
    EXPECT_EQ(lock.lockCount, 1);
    EXPECT_EQ(lock.unlockCount, 1);
}

TEST(WithLock, value_result_materialised_under_lock)
{
    // The copy is taken under the lock and the lock is free afterwards.
    std::mutex m;
    std::string guarded = "hello";
    auto const copy = withLock(m, [&] { return guarded; });
    EXPECT_EQ(copy, "hello");
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(WithLock, released_on_exception)
{
    std::mutex m;
    EXPECT_THROW(withLock(m, [] { throw std::runtime_error("boom"); }), std::runtime_error);
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(WithLock, guard_runs_exactly_once_on_exception)
{
    NoexceptLock lock;
    EXPECT_THROW(withLock(lock, [] { throw std::runtime_error("boom"); }), std::runtime_error);
    EXPECT_FALSE(lock.locked);
    EXPECT_EQ(lock.lockCount, 1);
    EXPECT_EQ(lock.unlockCount, 1);
}

TEST(WithLock, move_only_result_and_rvalue_args)
{
    std::mutex m;
    auto p = withLock(m, [](std::unique_ptr<int> q) { return q; }, std::make_unique<int>(7));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 7);
}

TEST(WithLock, lvalue_args_forwarded_as_lvalues)
{
    // Non-const reference arguments are supported: the callable mutates the
    // caller's object, not a copy.
    std::mutex m;
    int x = 0;
    withLock(m, [](int& v) { v = 5; }, x);
    EXPECT_EQ(x, 5);
}

TEST(WithLock, member_function_pointer_callable)
{
    std::mutex m;
    Counter c;
    auto const r = withLock(m, &Counter::add, &c, 3);
    EXPECT_EQ(r, 3);
    EXPECT_EQ(c.value, 3);
}

TEST(WithLock, concurrent_increments_are_serialised)
{
    // Four threads each perform exactly 25 increments of a counter passed
    // by reference through the Args... path. Every increment returns the
    // post-increment value; if the critical sections were not serialised,
    // lost updates would show up as duplicate values and a final count
    // below 100.
    constexpr int threadCount = 4;
    constexpr int perThread = 25;
    constexpr int total = threadCount * perThread;

    std::mutex m;
    int counter = 0;
    std::array<std::vector<int>, threadCount> seen;
    std::latch start(threadCount);

    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&, t] {
                seen[t].reserve(perThread);
                start.arrive_and_wait();
                for (int i = 0; i < perThread; ++i)
                    seen[t].push_back(withLock(m, [](int& c) { return ++c; }, counter));
            });
        }
    }

    EXPECT_EQ(counter, total);

    std::vector<int> all;
    for (auto const& s : seen)
    {
        EXPECT_EQ(s.size(), static_cast<std::size_t>(perThread));
        all.insert(all.end(), s.begin(), s.end());
    }
    std::sort(all.begin(), all.end());

    std::vector<int> expected(total);
    std::iota(expected.begin(), expected.end(), 1);
    EXPECT_EQ(all, expected);
}

TEST(WithLockRef, identity_preserved)
{
    std::mutex m;
    std::string s = "hello";
    std::string& r = withLockRef(m, [&]() -> std::string& { return s; });
    EXPECT_EQ(&r, &s);
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(WithLockRef, member_function_pointer_callable)
{
    std::mutex m;
    Counter c;
    Counter& r = withLockRef(m, &Counter::self, &c);
    EXPECT_EQ(&r, &c);
}
