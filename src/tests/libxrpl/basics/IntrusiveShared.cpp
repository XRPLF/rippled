
#include <xrpl/basics/IntrusivePointer.h>    // IWYU pragma: keep
#include <xrpl/basics/IntrusivePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/IntrusiveRefCounts.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>  // IWYU pragma: keep
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <latch>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl::tests {

/**
Experimentally, we discovered that using std::barrier performs extremely
poorly (~1 hour vs ~1 minute to run the test suite) in certain macOS
environments. To unblock our macOS CI pipeline, we replaced std::barrier with a
custom mutex-based barrier (Barrier) that significantly improves performance
without compromising correctness. For future reference, if we ever consider
reintroducing std::barrier, the following configuration is known to exhibit the
problem:

    Model Name: Mac mini
    Model Identifier: Mac14,3
    Model Number: Z16K000R4LL/A
    Chip: Apple M2
    Total Number of Cores: 8 (4 performance and 4 efficiency)
    Memory: 24 GB
    System Firmware Version: 11881.41.5
    OS Loader Version: 11881.1.1
    Apple clang version 16.0.0 (clang-1600.0.26.3)
    Target: arm64-apple-darwin24.0.0
    Thread model: posix

 */
struct Barrier
{
    std::mutex mtx;
    std::condition_variable cv;
    int count;
    int const initial;

    Barrier(int n) : count(n), initial(n)
    {
    }

    void
    arriveAndWait()
    {
        std::unique_lock lock(mtx);
        if (--count == 0)
        {
            count = initial;
            cv.notify_all();
        }
        else
        {
            cv.wait(lock, [&] { return count == initial; });
        }
    }
};

namespace {
enum class TrackedState : std::uint8_t {
    Uninitialized,
    Alive,
    PartiallyDeletedStarted,
    PartiallyDeleted,
    DeletedStarted,
    Deleted
};

class TIBase : public IntrusiveRefCounts
{
public:
    static constexpr std::size_t kMAX_STATES = 128;
    static std::array<std::atomic<TrackedState>, kMAX_STATES> state;
    static std::atomic<int> nextId;
    static TrackedState
    getState(int id)
    {
        assert(id < state.size());
        return state[id].load(std::memory_order_acquire);
    }
    static void
    resetStates(bool resetCallback)
    {
        for (int i = 0; i < kMAX_STATES; ++i)
        {
            state[i].store(TrackedState::Uninitialized, std::memory_order_release);
        }
        nextId.store(0, std::memory_order_release);
        if (resetCallback)
            TIBase::tracingCallback = [](TrackedState, std::optional<TrackedState>) {};
    }

    struct ResetStatesGuard
    {
        bool resetCallback{false};

        ResetStatesGuard(bool resetCallback) : resetCallback{resetCallback}
        {
            TIBase::resetStates(resetCallback);
        }
        ~ResetStatesGuard()
        {
            TIBase::resetStates(resetCallback);
        }
    };

    TIBase() : id{checkoutID()}
    {
        assert(state.size() > id);
        state[id].store(TrackedState::Alive, std::memory_order_relaxed);
    }
    ~TIBase() override
    {
        using enum TrackedState;

        assert(state.size() > id);
        tracingCallback(state[id].load(std::memory_order_relaxed), DeletedStarted);

        assert(state.size() > id);
        // Use relaxed memory order to try to avoid atomic operations from
        // adding additional memory synchronizations that may hide threading
        // errors in the underlying shared pointer class.
        state[id].store(DeletedStarted, std::memory_order_relaxed);

        tracingCallback(DeletedStarted, Deleted);

        assert(state.size() > id);
        state[id].store(TrackedState::Deleted, std::memory_order_relaxed);

        tracingCallback(TrackedState::Deleted, std::nullopt);
    }

    void
    partialDestructor() const
    {
        using enum TrackedState;

        assert(state.size() > id);
        tracingCallback(state[id].load(std::memory_order_relaxed), PartiallyDeletedStarted);

        assert(state.size() > id);
        state[id].store(PartiallyDeletedStarted, std::memory_order_relaxed);

        tracingCallback(PartiallyDeletedStarted, PartiallyDeleted);

        assert(state.size() > id);
        state[id].store(PartiallyDeleted, std::memory_order_relaxed);

        tracingCallback(PartiallyDeleted, std::nullopt);
    }

    static std::function<void(TrackedState, std::optional<TrackedState>)> tracingCallback;

    int id;

private:
    static int
    checkoutID()
    {
        return nextId.fetch_add(1, std::memory_order_acq_rel);
    }
};

std::array<std::atomic<TrackedState>, TIBase::kMAX_STATES> TIBase::state;
std::atomic<int> TIBase::nextId{0};

std::function<void(TrackedState, std::optional<TrackedState>)> TIBase::tracingCallback =
    [](TrackedState, std::optional<TrackedState>) {};

}  // namespace

class IntrusiveSharedTest : public ::testing::Test
{
public:
    static void
    testBasics()
    {
        {
            TIBase::ResetStatesGuard const rsg{true};

            TIBase const b;
            EXPECT_EQ(b.useCount(), 1);
            b.addWeakRef();
            EXPECT_EQ(b.useCount(), 1);
            auto s = b.releaseStrongRef();
            EXPECT_EQ(s, ReleaseStrongRefAction::PartialDestroy);
            EXPECT_EQ(b.useCount(), 0);
            TIBase const* pb = &b;
            partialDestructorFinished(&pb);
            EXPECT_TRUE(!pb);
            auto w = b.releaseWeakRef();
            EXPECT_EQ(w, ReleaseWeakRefAction::Destroy);
        }

        std::vector<SharedIntrusive<TIBase>> strong;
        std::vector<WeakIntrusive<TIBase>> weak;
        {
            TIBase::ResetStatesGuard const rsg{true};

            using enum TrackedState;
            auto b = makeSharedIntrusive<TIBase>();
            auto id = b->id;
            EXPECT_EQ(TIBase::getState(id), Alive);
            EXPECT_EQ(b->useCount(), 1);
            for (int i = 0; i < 10; ++i)
            {
                strong.push_back(b);
            }
            b.reset();
            EXPECT_EQ(TIBase::getState(id), Alive);
            strong.resize(strong.size() - 1);
            EXPECT_EQ(TIBase::getState(id), Alive);
            strong.clear();
            EXPECT_EQ(TIBase::getState(id), Deleted);

            b = makeSharedIntrusive<TIBase>();
            id = b->id;
            EXPECT_EQ(TIBase::getState(id), Alive);
            EXPECT_EQ(b->useCount(), 1);
            for (int i = 0; i < 10; ++i)
            {
                weak.emplace_back(b);
                EXPECT_EQ(b->useCount(), 1);
            }
            EXPECT_EQ(TIBase::getState(id), Alive);
            weak.resize(weak.size() - 1);
            EXPECT_EQ(TIBase::getState(id), Alive);
            b.reset();
            EXPECT_EQ(TIBase::getState(id), PartiallyDeleted);
            while (!weak.empty())
            {
                weak.resize(weak.size() - 1);
                if (!weak.empty())
                {
                    EXPECT_EQ(TIBase::getState(id), PartiallyDeleted);
                }
            }
            EXPECT_EQ(TIBase::getState(id), Deleted);
        }
        {
            TIBase::ResetStatesGuard const rsg{true};

            using enum TrackedState;
            auto b = makeSharedIntrusive<TIBase>();
            auto id = b->id;
            EXPECT_EQ(TIBase::getState(id), Alive);
            WeakIntrusive<TIBase> w{b};
            EXPECT_EQ(TIBase::getState(id), Alive);
            auto s = w.lock();
            EXPECT_TRUE(s && s->useCount() == 2);
            b.reset();
            EXPECT_TRUE(TIBase::getState(id) == Alive);
            EXPECT_TRUE(s && s->useCount() == 1);
            s.reset();
            EXPECT_EQ(TIBase::getState(id), PartiallyDeleted);
            EXPECT_TRUE(w.expired());
            s = w.lock();
            // Cannot convert a weak pointer to a strong pointer if object is
            // already partially deleted
            EXPECT_TRUE(!s);
            w.reset();
            EXPECT_EQ(TIBase::getState(id), Deleted);
        }
        {
            TIBase::ResetStatesGuard const rsg{true};

            using enum TrackedState;
            using swu = SharedWeakUnion<TIBase>;
            swu b = makeSharedIntrusive<TIBase>();
            EXPECT_TRUE(b.isStrong() && b.useCount() == 1);
            auto id = b.get()->id;
            EXPECT_EQ(TIBase::getState(id), Alive);
            swu w = b;
            EXPECT_TRUE(TIBase::getState(id) == Alive);
            EXPECT_TRUE(w.isStrong() && b.useCount() == 2);
            w.convertToWeak();
            EXPECT_TRUE(w.isWeak() && b.useCount() == 1);
            swu s = w;
            EXPECT_TRUE(s.isWeak() && b.useCount() == 1);
            s.convertToStrong();
            EXPECT_TRUE(s.isStrong() && b.useCount() == 2);
            b.reset();
            EXPECT_EQ(TIBase::getState(id), Alive);
            EXPECT_EQ(s.useCount(), 1);
            EXPECT_TRUE(!w.expired());
            s.reset();
            EXPECT_EQ(TIBase::getState(id), PartiallyDeleted);
            EXPECT_TRUE(w.expired());
            w.convertToStrong();
            // Cannot convert a weak pointer to a strong pointer if object is
            // already partially deleted
            EXPECT_TRUE(w.isWeak());
            w.reset();
            EXPECT_EQ(TIBase::getState(id), Deleted);
        }
        {
            // Testing SharedWeakUnion assignment operator

            TIBase::ResetStatesGuard const rsg{true};

            auto strong1 = makeSharedIntrusive<TIBase>();
            auto strong2 = makeSharedIntrusive<TIBase>();

            auto id1 = strong1->id;
            auto id2 = strong2->id;

            EXPECT_NE(id1, id2);

            SharedWeakUnion<TIBase> union1 = strong1;
            SharedWeakUnion<TIBase> union2 = strong2;

            EXPECT_TRUE(union1.isStrong());
            EXPECT_TRUE(union2.isStrong());
            EXPECT_EQ(union1.get(), strong1.get());
            EXPECT_EQ(union2.get(), strong2.get());

            // 1) Normal assignment: explicitly calls SharedWeakUnion assignment
            union1 = union2;
            EXPECT_TRUE(union1.isStrong());
            EXPECT_TRUE(union2.isStrong());
            EXPECT_EQ(union1.get(), union2.get());
            EXPECT_EQ(TIBase::getState(id1), TrackedState::Alive);
            EXPECT_EQ(TIBase::getState(id2), TrackedState::Alive);

            // 2) Test self-assignment
            EXPECT_TRUE(union1.isStrong());
            EXPECT_EQ(TIBase::getState(id1), TrackedState::Alive);
            int const initialRefCount = strong1->useCount();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
            union1 = union1;  // Self-assignment
#pragma clang diagnostic pop
            EXPECT_TRUE(union1.isStrong());
            EXPECT_EQ(TIBase::getState(id1), TrackedState::Alive);
            EXPECT_EQ(strong1->useCount(), initialRefCount);

            // 3) Test assignment from null union pointer
            union1 = SharedWeakUnion<TIBase>();
            EXPECT_EQ(union1.get(), nullptr);

            // 4) Test assignment to expired union pointer
            strong2.reset();
            union2.reset();
            union1 = union2;
            EXPECT_EQ(union1.get(), nullptr);
            EXPECT_EQ(TIBase::getState(id2), TrackedState::Deleted);
        }
    }

    static void
    testPartialDelete()
    {
        // This test creates two threads. One with a strong pointer and one
        // with a weak pointer. The strong pointer is reset while the weak
        // pointer still holds a reference, triggering a partial delete.
        // While the partial delete function runs (a sleep is inserted) the
        // weak pointer is reset. The destructor should wait to run until
        // after the partial delete function has completed running.

        using enum TrackedState;

        TIBase::ResetStatesGuard const rsg{true};

        auto strong = makeSharedIntrusive<TIBase>();
        WeakIntrusive<TIBase> weak{strong};
        bool destructorRan = false;
        bool partialDeleteRan = false;
        std::latch partialDeleteStartedSyncPoint{2};
        strong->tracingCallback = [&](TrackedState cur, std::optional<TrackedState> next) {
            using enum TrackedState;
            if (next == DeletedStarted)
            {
                // strong goes out of scope while weak is still in scope
                // This checks that partialDelete has run to completion
                // before the destructor is called. A sleep is inserted
                // inside the partial delete to make sure the destructor is
                // given an opportunity to run during partial delete.
                EXPECT_EQ(cur, PartiallyDeleted);
            }
            if (next == PartiallyDeletedStarted)
            {
                partialDeleteStartedSyncPoint.arrive_and_wait();
                using namespace std::chrono_literals;
                // Sleep and let the weak pointer go out of scope,
                // potentially triggering a destructor while partial delete
                // is running. The test is to make sure that doesn't happen.
                std::this_thread::sleep_for(800ms);
            }
            if (next == PartiallyDeleted)
            {
                EXPECT_TRUE(!partialDeleteRan && !destructorRan);
                partialDeleteRan = true;
            }
            if (next == Deleted)
            {
                EXPECT_TRUE(!destructorRan);
                destructorRan = true;
            }
        };
        std::thread t1{[&] {
            partialDeleteStartedSyncPoint.arrive_and_wait();
            weak.reset();  // Trigger a full delete as soon as the partial
                           // delete starts
        }};
        std::thread t2{[&] {
            strong.reset();  // Trigger a partial delete
        }};
        t1.join();
        t2.join();

        EXPECT_TRUE(destructorRan && partialDeleteRan);
    }

    static void
    testDestructor()
    {
        // This test creates two threads. One with a strong pointer and one
        // with a weak pointer. The weak pointer is reset while the strong
        // pointer still holds a reference. Then the strong pointer is
        // reset. Only the destructor should run. The partial destructor
        // should not be called. Since the weak reset runs to completion
        // before the strong pointer is reset, threading doesn't add much to
        // this test, but there is no harm in keeping it.

        using enum TrackedState;

        TIBase::ResetStatesGuard const rsg{true};

        auto strong = makeSharedIntrusive<TIBase>();
        WeakIntrusive<TIBase> weak{strong};
        bool destructorRan = false;
        bool partialDeleteRan = false;
        std::latch weakResetSyncPoint{2};
        strong->tracingCallback = [&](TrackedState cur, std::optional<TrackedState> next) {
            using enum TrackedState;
            if (next == PartiallyDeleted)
            {
                EXPECT_TRUE(!partialDeleteRan && !destructorRan);
                partialDeleteRan = true;
            }
            if (next == Deleted)
            {
                EXPECT_TRUE(!destructorRan);
                destructorRan = true;
            }
        };
        std::thread t1{[&] {
            weak.reset();
            weakResetSyncPoint.arrive_and_wait();
        }};
        std::thread t2{[&] {
            weakResetSyncPoint.arrive_and_wait();
            strong.reset();  // Trigger a partial delete
        }};
        t1.join();
        t2.join();

        EXPECT_TRUE(destructorRan && !partialDeleteRan);
    }

    static void
    testMultithreadedClearMixedVariant()
    {
        // This test creates and destroys many strong and weak pointers in a
        // loop. There is a random mix of strong and weak pointers stored in
        // a vector (held as a variant). Both threads clear all the pointers
        // and check that the invariants hold.

        using enum TrackedState;
        TIBase::ResetStatesGuard const rsg{true};

        std::atomic<int> destructionState{0};
        // returns destructorRan and partialDestructorRan (in that order)
        auto getDestructorState = [&]() -> std::pair<bool, bool> {
            int const s = destructionState.load(std::memory_order_relaxed);
            return {(s & 1) != 0, (s & 2) != 0};
        };
        auto setDestructorRan = [&]() -> void {
            destructionState.fetch_or(1, std::memory_order_acq_rel);
        };
        auto setPartialDeleteRan = [&]() -> void {
            destructionState.fetch_or(2, std::memory_order_acq_rel);
        };
        auto tracingCallback = [&](TrackedState cur, std::optional<TrackedState> next) {
            using enum TrackedState;
            auto [destructorRan, partialDeleteRan] = getDestructorState();
            if (next == PartiallyDeleted)
            {
                EXPECT_TRUE(!partialDeleteRan && !destructorRan);
                setPartialDeleteRan();
            }
            if (next == Deleted)
            {
                EXPECT_TRUE(!destructorRan);
                setDestructorRan();
            }
        };
        auto createVecOfPointers = [&](auto const& toClone, std::default_random_engine& eng)
            -> std::vector<std::variant<SharedIntrusive<TIBase>, WeakIntrusive<TIBase>>> {
            std::vector<std::variant<SharedIntrusive<TIBase>, WeakIntrusive<TIBase>>> result;
            std::uniform_int_distribution<> toCreateDist(4, 64);
            std::uniform_int_distribution<> isStrongDist(0, 1);
            auto numToCreate = toCreateDist(eng);
            result.reserve(numToCreate);
            for (int i = 0; i < numToCreate; ++i)
            {
                if (isStrongDist(eng))
                {
                    result.emplace_back(SharedIntrusive<TIBase>(toClone));
                }
                else
                {
                    result.emplace_back(WeakIntrusive<TIBase>(toClone));
                }
            }
            return result;
        };
        constexpr int kLOOP_ITERS = 2 * 1024;
        constexpr int kNUM_THREADS = 16;
        std::vector<SharedIntrusive<TIBase>> toClone;
        Barrier loopStartSyncPoint{kNUM_THREADS};
        Barrier postCreateToCloneSyncPoint{kNUM_THREADS};
        Barrier postCreateVecOfPointersSyncPoint{kNUM_THREADS};
        auto engines = [&]() -> std::vector<std::default_random_engine> {
            std::random_device rd;
            std::vector<std::default_random_engine> result;
            result.reserve(kNUM_THREADS);
            for (int i = 0; i < kNUM_THREADS; ++i)
                result.emplace_back(rd());
            return result;
        }();

        // cloneAndDestroy clones the strong pointer into a vector of mixed
        // strong and weak pointers and destroys them all at once.
        // threadId==0 is special.
        auto cloneAndDestroy = [&](int threadId) {
            for (int i = 0; i < kLOOP_ITERS; ++i)
            {
                // ------ Sync Point ------
                loopStartSyncPoint.arriveAndWait();

                // only thread 0 should reset the state
                std::optional<TIBase::ResetStatesGuard> rsg;
                if (threadId == 0)
                {
                    // Thread 0 is the genesis thread. It creates the strong
                    // pointers to be cloned by the other threads. This
                    // thread will also check that the destructor ran and
                    // clear the temporary variables.

                    rsg.emplace(false);
                    auto [destructorRan, partialDeleteRan] = getDestructorState();
                    EXPECT_TRUE(!i || destructorRan);
                    destructionState.store(0, std::memory_order_release);

                    toClone.clear();
                    toClone.resize(kNUM_THREADS);
                    auto strong = makeSharedIntrusive<TIBase>();
                    strong->tracingCallback = tracingCallback;
                    std::ranges::fill(toClone, strong);
                }

                // ------ Sync Point ------
                postCreateToCloneSyncPoint.arriveAndWait();

                auto v = createVecOfPointers(toClone[threadId], engines[threadId]);
                toClone[threadId].reset();

                // ------ Sync Point ------
                postCreateVecOfPointersSyncPoint.arriveAndWait();

                v.clear();
            }
        };
        std::vector<std::thread> threads;
        threads.reserve(kNUM_THREADS);
        for (int i = 0; i < kNUM_THREADS; ++i)
        {
            threads.emplace_back(cloneAndDestroy, i);
        }
        for (int i = 0; i < kNUM_THREADS; ++i)
        {
            threads[i].join();
        }
    }

    static void
    testMultithreadedClearMixedUnion()
    {
        // This test creates and destroys many SharedWeak pointers in a
        // loop. All the pointers start as strong and a loop randomly
        // convert them between strong and weak pointers. Both threads clear
        // all the pointers and check that the invariants hold.
        //
        // Note: This test also differs from the test above in that the pointers
        // randomly change from strong to weak and from weak to strong in a
        // loop. This can't be done in the variant test above because variant is
        // not thread safe while the SharedWeakUnion is thread safe.

        using enum TrackedState;

        TIBase::ResetStatesGuard const rsg{true};

        std::atomic<int> destructionState{0};
        // returns destructorRan and partialDestructorRan (in that order)
        auto getDestructorState = [&]() -> std::pair<bool, bool> {
            int const s = destructionState.load(std::memory_order_relaxed);
            return {(s & 1) != 0, (s & 2) != 0};
        };
        auto setDestructorRan = [&]() -> void {
            destructionState.fetch_or(1, std::memory_order_acq_rel);
        };
        auto setPartialDeleteRan = [&]() -> void {
            destructionState.fetch_or(2, std::memory_order_acq_rel);
        };
        auto tracingCallback = [&](TrackedState cur, std::optional<TrackedState> next) {
            using enum TrackedState;
            auto [destructorRan, partialDeleteRan] = getDestructorState();
            if (next == PartiallyDeleted)
            {
                EXPECT_TRUE(!partialDeleteRan && !destructorRan);
                setPartialDeleteRan();
            }
            if (next == Deleted)
            {
                EXPECT_TRUE(!destructorRan);
                setDestructorRan();
            }
        };
        auto createVecOfPointers =
            [&](auto const& toClone,
                std::default_random_engine& eng) -> std::vector<SharedWeakUnion<TIBase>> {
            std::vector<SharedWeakUnion<TIBase>> result;
            std::uniform_int_distribution<> toCreateDist(4, 64);
            auto numToCreate = toCreateDist(eng);
            result.reserve(numToCreate);
            for (int i = 0; i < numToCreate; ++i)
                result.emplace_back(SharedIntrusive<TIBase>(toClone));
            return result;
        };
        constexpr int kLOOP_ITERS = 2 * 1024;
        constexpr int kFLIP_POINTERS_LOOP_ITERS = 256;
        constexpr int kNUM_THREADS = 16;
        std::vector<SharedIntrusive<TIBase>> toClone;
        Barrier loopStartSyncPoint{kNUM_THREADS};
        Barrier postCreateToCloneSyncPoint{kNUM_THREADS};
        Barrier postCreateVecOfPointersSyncPoint{kNUM_THREADS};
        Barrier postFlipPointersLoopSyncPoint{kNUM_THREADS};
        auto engines = [&]() -> std::vector<std::default_random_engine> {
            std::random_device rd;
            std::vector<std::default_random_engine> result;
            result.reserve(kNUM_THREADS);
            for (int i = 0; i < kNUM_THREADS; ++i)
                result.emplace_back(rd());
            return result;
        }();

        // cloneAndDestroy clones the strong pointer into a vector of
        // mixed strong and weak pointers, runs a loop that randomly
        // changes strong pointers to weak pointers,  and destroys them
        // all at once.
        auto cloneAndDestroy = [&](int threadId) {
            for (int i = 0; i < kLOOP_ITERS; ++i)
            {
                // ------ Sync Point ------
                loopStartSyncPoint.arriveAndWait();

                // only thread 0 should reset the state
                std::optional<TIBase::ResetStatesGuard> rsg;
                if (threadId == 0)
                {
                    // threadId 0 is the genesis thread. It creates the
                    // strong point to be cloned by the other threads. This
                    // thread will also check that the destructor ran and
                    // clear the temporary variables.
                    rsg.emplace(false);
                    auto [destructorRan, partialDeleteRan] = getDestructorState();
                    EXPECT_TRUE(!i || destructorRan);
                    destructionState.store(0, std::memory_order_release);

                    toClone.clear();
                    toClone.resize(kNUM_THREADS);
                    auto strong = makeSharedIntrusive<TIBase>();
                    strong->tracingCallback = tracingCallback;
                    std::ranges::fill(toClone, strong);
                }

                // ------ Sync Point ------
                postCreateToCloneSyncPoint.arriveAndWait();

                auto v = createVecOfPointers(toClone[threadId], engines[threadId]);
                toClone[threadId].reset();

                // ------ Sync Point ------
                postCreateVecOfPointersSyncPoint.arriveAndWait();

                std::uniform_int_distribution<> isStrongDist(0, 1);
                for (int f = 0; f < kFLIP_POINTERS_LOOP_ITERS; ++f)
                {
                    for (auto& p : v)
                    {
                        if (isStrongDist(engines[threadId]))
                        {
                            p.convertToStrong();
                        }
                        else
                        {
                            p.convertToWeak();
                        }
                    }
                }

                // ------ Sync Point ------
                postFlipPointersLoopSyncPoint.arriveAndWait();

                v.clear();
            }
        };
        std::vector<std::thread> threads;
        threads.reserve(kNUM_THREADS);
        for (int i = 0; i < kNUM_THREADS; ++i)
        {
            threads.emplace_back(cloneAndDestroy, i);
        }
        for (int i = 0; i < kNUM_THREADS; ++i)
        {
            threads[i].join();
        }
    }

    static void
    testMultithreadedLockingWeak()
    {
        // This test creates a single shared atomic pointer that multiple thread
        // create weak pointers from. The threads then lock the weak pointers.
        // Both threads clear all the pointers and check that the invariants
        // hold.

        using enum TrackedState;

        TIBase::ResetStatesGuard const rsg{true};

        std::atomic<int> destructionState{0};
        // returns destructorRan and partialDestructorRan (in that order)
        auto getDestructorState = [&]() -> std::pair<bool, bool> {
            int const s = destructionState.load(std::memory_order_relaxed);
            return {(s & 1) != 0, (s & 2) != 0};
        };
        auto setDestructorRan = [&]() -> void {
            destructionState.fetch_or(1, std::memory_order_acq_rel);
        };
        auto setPartialDeleteRan = [&]() -> void {
            destructionState.fetch_or(2, std::memory_order_acq_rel);
        };
        auto tracingCallback = [&](TrackedState cur, std::optional<TrackedState> next) {
            using enum TrackedState;
            auto [destructorRan, partialDeleteRan] = getDestructorState();
            if (next == PartiallyDeleted)
            {
                EXPECT_TRUE(!partialDeleteRan && !destructorRan);
                setPartialDeleteRan();
            }
            if (next == Deleted)
            {
                EXPECT_TRUE(!destructorRan);
                setDestructorRan();
            }
        };

        constexpr int kLOOP_ITERS = 2 * 1024;
        constexpr int kLOCK_WEAK_LOOP_ITERS = 256;
        constexpr int kNUM_THREADS = 16;
        std::vector<SharedIntrusive<TIBase>> toLock;
        Barrier loopStartSyncPoint{kNUM_THREADS};
        Barrier postCreateToLockSyncPoint{kNUM_THREADS};
        Barrier postLockWeakLoopSyncPoint{kNUM_THREADS};

        // lockAndDestroy creates weak pointers from the strong pointer
        // and runs a loop that locks the weak pointer. At the end of the loop
        // all the pointers are destroyed all at once.
        auto lockAndDestroy = [&](int threadId) {
            for (int i = 0; i < kLOOP_ITERS; ++i)
            {
                // ------ Sync Point ------
                loopStartSyncPoint.arriveAndWait();

                // only thread 0 should reset the state
                std::optional<TIBase::ResetStatesGuard> rsg;
                if (threadId == 0)
                {
                    // threadId 0 is the genesis thread. It creates the
                    // strong point to be locked by the other threads. This
                    // thread will also check that the destructor ran and
                    // clear the temporary variables.
                    rsg.emplace(false);
                    auto [destructorRan, partialDeleteRan] = getDestructorState();
                    EXPECT_TRUE(!i || destructorRan);
                    destructionState.store(0, std::memory_order_release);

                    toLock.clear();
                    toLock.resize(kNUM_THREADS);
                    auto strong = makeSharedIntrusive<TIBase>();
                    strong->tracingCallback = tracingCallback;
                    std::ranges::fill(toLock, strong);
                }

                // ------ Sync Point ------
                postCreateToLockSyncPoint.arriveAndWait();

                // Multiple threads all create a weak pointer from the same
                // strong pointer
                WeakIntrusive const weak{toLock[threadId]};
                for (int wi = 0; wi < kLOCK_WEAK_LOOP_ITERS; ++wi)
                {
                    EXPECT_TRUE(!weak.expired());
                    auto strong = weak.lock();
                    EXPECT_TRUE(strong);
                }

                // ------ Sync Point ------
                postLockWeakLoopSyncPoint.arriveAndWait();

                toLock[threadId].reset();
            }
        };
        std::vector<std::thread> threads;
        threads.reserve(kNUM_THREADS);
        for (int i = 0; i < kNUM_THREADS; ++i)
        {
            threads.emplace_back(lockAndDestroy, i);
        }
        for (int i = 0; i < kNUM_THREADS; ++i)
        {
            threads[i].join();
        }
    }

    static void
    run()
    {
        testBasics();
        testPartialDelete();
        testDestructor();
        testMultithreadedClearMixedVariant();
        testMultithreadedClearMixedUnion();
        testMultithreadedLockingWeak();
    }
};

TEST_F(IntrusiveSharedTest, intrusive_shared)
{
    run();
}

}  // namespace xrpl::tests
