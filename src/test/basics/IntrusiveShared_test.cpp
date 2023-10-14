#include <ripple/basics/IntrusivePointer.ipp>
#include <ripple/basics/IntrusiveRefCounts.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>
#include <test/unit_test/SuiteJournal.h>

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <latch>
#include <random>
#include <string>
#include <thread>

namespace ripple {
namespace tests {

namespace {
enum class TrackedState : std::uint8_t {
    uninitialized,
    alive,
    partiallyDeletedStarted,
    partiallyDeleted,
    deletedStarted,
    deleted
};

class TIBase : public IntrusiveRefCounts
{
public:
    static constexpr std::size_t maxStates = 128;
    static std::array<std::atomic<TrackedState>, maxStates> state;
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
        for (int i = 0; i < maxStates; ++i)
        {
            state[i].store(
                TrackedState::uninitialized, std::memory_order_release);
        }
        nextId.store(0, std::memory_order_release);
        if (resetCallback)
            TIBase::tracingCallback_ = [](TrackedState,
                                          std::optional<TrackedState>) {};
    }

    struct ResetStatesGuard
    {
        bool resetCallback_{false};

        ResetStatesGuard(bool resetCallback) : resetCallback_{resetCallback}
        {
            TIBase::resetStates(resetCallback_);
        }
        ~ResetStatesGuard()
        {
            TIBase::resetStates(resetCallback_);
        }
    };

    TIBase() : id_{checkoutID()}
    {
        assert(state.size() > id_);
        state[id_].store(TrackedState::alive, std::memory_order_relaxed);
    }
    ~TIBase()
    {
        using enum TrackedState;

        assert(state.size() > id_);
        tracingCallback_(
            state[id_].load(std::memory_order_relaxed), deletedStarted);

        assert(state.size() > id_);
        // Use relaxed memory order to try to avoid atomic operations from
        // adding additional memory synchronizations that may hide threading
        // errors in the underlying shared pointer class.
        state[id_].store(deletedStarted, std::memory_order_relaxed);

        tracingCallback_(deletedStarted, deleted);

        assert(state.size() > id_);
        state[id_].store(TrackedState::deleted, std::memory_order_relaxed);

        tracingCallback_(TrackedState::deleted, std::nullopt);
    }

    void
    partialDestructor()
    {
        using enum TrackedState;

        assert(state.size() > id_);
        tracingCallback_(
            state[id_].load(std::memory_order_relaxed),
            partiallyDeletedStarted);

        assert(state.size() > id_);
        state[id_].store(partiallyDeletedStarted, std::memory_order_relaxed);

        tracingCallback_(partiallyDeletedStarted, partiallyDeleted);

        assert(state.size() > id_);
        state[id_].store(partiallyDeleted, std::memory_order_relaxed);

        tracingCallback_(partiallyDeleted, std::nullopt);
    }

    static std::function<void(TrackedState, std::optional<TrackedState>)>
        tracingCallback_;

    int id_;

private:
    static int
    checkoutID()
    {
        return nextId.fetch_add(1, std::memory_order_acq_rel);
    }
};

std::array<std::atomic<TrackedState>, TIBase::maxStates> TIBase::state;
std::atomic<int> TIBase::nextId{0};

std::function<void(TrackedState, std::optional<TrackedState>)>
    TIBase::tracingCallback_ = [](TrackedState, std::optional<TrackedState>) {};

}  // namespace

class IntrusiveShared_test : public beast::unit_test::suite
{
public:
    void
    testBasics()
    {
        testcase("Basics");

        {
            TIBase::ResetStatesGuard rsg{true};

            TIBase b;
            BEAST_EXPECT(b.use_count() == 1);
            b.addWeakRef();
            BEAST_EXPECT(b.use_count() == 1);
            auto a = b.releaseStrongRef();
            BEAST_EXPECT(a == ReleaseRefAction::partialDestroy);
            BEAST_EXPECT(b.use_count() == 0);
            TIBase* pb = &b;
            partialDestructorFinished(&pb);
            BEAST_EXPECT(!pb);
            a = b.releaseWeakRef();
            BEAST_EXPECT(a == ReleaseRefAction::destroy);
        }

        std::vector<SharedIntrusive<TIBase>> strong;
        std::vector<WeakIntrusive<TIBase>> weak;
        {
            TIBase::ResetStatesGuard rsg{true};

            using enum TrackedState;
            auto b = make_SharedIntrusive<TIBase>();
            auto id = b->id_;
            BEAST_EXPECT(TIBase::getState(id) == alive);
            BEAST_EXPECT(b->use_count() == 1);
            for (int i = 0; i < 10; ++i)
            {
                strong.push_back(b);
            }
            b.reset();
            BEAST_EXPECT(TIBase::getState(id) == alive);
            strong.resize(strong.size() - 1);
            BEAST_EXPECT(TIBase::getState(id) == alive);
            strong.clear();
            BEAST_EXPECT(TIBase::getState(id) == deleted);

            b = make_SharedIntrusive<TIBase>();
            id = b->id_;
            BEAST_EXPECT(TIBase::getState(id) == alive);
            BEAST_EXPECT(b->use_count() == 1);
            for (int i = 0; i < 10; ++i)
            {
                weak.push_back(b);
                BEAST_EXPECT(b->use_count() == 1);
            }
            BEAST_EXPECT(TIBase::getState(id) == alive);
            weak.resize(weak.size() - 1);
            BEAST_EXPECT(TIBase::getState(id) == alive);
            b.reset();
            BEAST_EXPECT(TIBase::getState(id) == partiallyDeleted);
            while (!weak.empty())
            {
                weak.resize(weak.size() - 1);
                if (weak.size())
                    BEAST_EXPECT(TIBase::getState(id) == partiallyDeleted);
            }
            BEAST_EXPECT(TIBase::getState(id) == deleted);
        }
        {
            TIBase::ResetStatesGuard rsg{true};

            using enum TrackedState;
            auto b = make_SharedIntrusive<TIBase>();
            auto id = b->id_;
            BEAST_EXPECT(TIBase::getState(id) == alive);
            WeakIntrusive<TIBase> w{b};
            BEAST_EXPECT(TIBase::getState(id) == alive);
            auto s = w.lock();
            BEAST_EXPECT(s && s->use_count() == 2);
            b.reset();
            BEAST_EXPECT(TIBase::getState(id) == alive);
            BEAST_EXPECT(s && s->use_count() == 1);
            s.reset();
            BEAST_EXPECT(TIBase::getState(id) == partiallyDeleted);
            BEAST_EXPECT(w.expired());
            s = w.lock();
            // Cannot convert a weak pointer to a strong pointer if object is
            // already partially deleted
            BEAST_EXPECT(!s);
            w.reset();
            BEAST_EXPECT(TIBase::getState(id) == deleted);
        }
        {
            TIBase::ResetStatesGuard rsg{true};

            using enum TrackedState;
            using swu = SharedWeakUnion<TIBase>;
            swu b = make_SharedIntrusive<TIBase>();
            BEAST_EXPECT(b.isStrong() && b.use_count() == 1);
            auto id = b.get()->id_;
            BEAST_EXPECT(TIBase::getState(id) == alive);
            swu w = b;
            BEAST_EXPECT(TIBase::getState(id) == alive);
            BEAST_EXPECT(w.isStrong() && b.use_count() == 2);
            w.convertToWeak();
            BEAST_EXPECT(w.isWeak() && b.use_count() == 1);
            swu s = w;
            BEAST_EXPECT(s.isWeak() && b.use_count() == 1);
            s.convertToStrong();
            BEAST_EXPECT(s.isStrong() && b.use_count() == 2);
            b.reset();
            BEAST_EXPECT(TIBase::getState(id) == alive);
            BEAST_EXPECT(s.use_count() == 1);
            BEAST_EXPECT(!w.expired());
            s.reset();
            BEAST_EXPECT(TIBase::getState(id) == partiallyDeleted);
            BEAST_EXPECT(w.expired());
            w.convertToStrong();
            // Cannot convert a weak pointer to a strong pointer if object is
            // already partially deleted
            BEAST_EXPECT(w.isWeak());
            w.reset();
            BEAST_EXPECT(TIBase::getState(id) == deleted);
        }
    }

    void
    testPartialDelete()
    {
        testcase("Partial Delete");

        // This test creates two threads. One with a strong pointer and one
        // with a weak pointer. The strong pointer is reset while the weak
        // pointer still holds a reference, triggering a partial delete.
        // While the partial delete function runs (a sleep is inserted) the
        // weak pointer is reset. The destructor should wait to run until
        // after the partial delete function has completed running.

        using enum TrackedState;

        TIBase::ResetStatesGuard rsg{true};

        auto strong = make_SharedIntrusive<TIBase>();
        WeakIntrusive<TIBase> weak{strong};
        bool destructorRan = false;
        bool partialDeleteRan = false;
        std::latch partialDeleteStartedSyncPoint{2};
        strong->tracingCallback_ = [&](TrackedState cur,
                                       std::optional<TrackedState> next) {
            using enum TrackedState;
            if (next == deletedStarted)
            {
                // strong goes out of scope while weak is still in scope
                // This checks that partialDelete has run to completion
                // before the desturctor is called. A sleep is inserted
                // inside the partial delete to make sure the destructor is
                // given an opportunity to run durring partial delete.
                BEAST_EXPECT(cur == partiallyDeleted);
            }
            if (next == partiallyDeletedStarted)
            {
                partialDeleteStartedSyncPoint.arrive_and_wait();
                using namespace std::chrono_literals;
                // Sleep and let the weak pointer go out of scope,
                // potentially triggering a destructor while partial delete
                // is running. The test is to make sure that doesn't happen.
                std::this_thread::sleep_for(800ms);
            }
            if (next == partiallyDeleted)
            {
                BEAST_EXPECT(!partialDeleteRan && !destructorRan);
                partialDeleteRan = true;
            }
            if (next == deleted)
            {
                BEAST_EXPECT(!destructorRan);
                destructorRan = true;
            }
        };
        std::jthread t1{[&] {
            partialDeleteStartedSyncPoint.arrive_and_wait();
            weak.reset();  // Trigger a full delete as soon as the partial
                           // delete starts
        }};
        std::jthread t2{[&] {
            strong.reset();  // Trigger a partial delete
        }};
        t1.join();
        t2.join();

        BEAST_EXPECT(destructorRan && partialDeleteRan);
    }

    void
    testDestructor()
    {
        testcase("Destructor");

        // This test creates two threads. One with a strong pointer and one
        // with a weak pointer. The weak pointer is reset while the strong
        // pointer still holds a reference. Then the strong pointer is
        // reset. Only the destructor should run. The partial destructor
        // should not be called. Since the weak reset runs to completion
        // before the strong pointer is reset, threading doesn't add much to
        // this test, but there is no harm in keeping it.

        using enum TrackedState;

        TIBase::ResetStatesGuard rsg{true};

        auto strong = make_SharedIntrusive<TIBase>();
        WeakIntrusive<TIBase> weak{strong};
        bool destructorRan = false;
        bool partialDeleteRan = false;
        std::latch weakResetSyncPoint{2};
        strong->tracingCallback_ = [&](TrackedState cur,
                                       std::optional<TrackedState> next) {
            using enum TrackedState;
            if (next == partiallyDeleted)
            {
                BEAST_EXPECT(!partialDeleteRan && !destructorRan);
                partialDeleteRan = true;
            }
            if (next == deleted)
            {
                BEAST_EXPECT(!destructorRan);
                destructorRan = true;
            }
        };
        std::jthread t1{[&] {
            weak.reset();
            weakResetSyncPoint.arrive_and_wait();
        }};
        std::jthread t2{[&] {
            weakResetSyncPoint.arrive_and_wait();
            strong.reset();  // Trigger a partial delete
        }};
        t1.join();
        t2.join();

        BEAST_EXPECT(destructorRan && !partialDeleteRan);
    }

    void
    testMultithreadedClearMixedVariant()
    {
        testcase("Multithreaded Clear Mixed Variant");

        // This test creates and destroys many strong and weak pointers in a
        // loop. There is a random mix of strong and weak pointers stored in
        // a vector (held as a variant). Both threads clear all the pointers
        // and check that the invariants hold.

        using enum TrackedState;
        TIBase::ResetStatesGuard rsg{true};

        std::atomic<int> destructionState{0};
        // returns destructorRan and partialDestructorRan (in that order)
        auto getDestructorState = [&]() -> std::pair<bool, bool> {
            int s = destructionState.load(std::memory_order_relaxed);
            return {(s & 1) != 0, (s & 2) != 0};
        };
        auto setDestructorRan = [&]() -> void {
            destructionState.fetch_or(1, std::memory_order_acq_rel);
        };
        auto setPartialDeleteRan = [&]() -> void {
            destructionState.fetch_or(2, std::memory_order_acq_rel);
        };
        auto tracingCallback = [&](TrackedState cur,
                                   std::optional<TrackedState> next) {
            using enum TrackedState;
            auto [destructorRan, partialDeleteRan] = getDestructorState();
            if (next == partiallyDeleted)
            {
                BEAST_EXPECT(!partialDeleteRan && !destructorRan);
                setPartialDeleteRan();
            }
            if (next == deleted)
            {
                BEAST_EXPECT(!destructorRan);
                setDestructorRan();
            }
        };
        auto createVecOfPointers = [&](auto const& toClone,
                                       std::default_random_engine& eng)
            -> std::vector<
                std::variant<SharedIntrusive<TIBase>, WeakIntrusive<TIBase>>> {
            std::vector<
                std::variant<SharedIntrusive<TIBase>, WeakIntrusive<TIBase>>>
                result;
            std::uniform_int_distribution<> toCreateDist(4, 64);
            std::uniform_int_distribution<> isStrongDist(0, 1);
            auto numToCreate = toCreateDist(eng);
            result.reserve(numToCreate);
            for (int i = 0; i < numToCreate; ++i)
            {
                if (isStrongDist(eng))
                {
                    result.push_back(SharedIntrusive<TIBase>(toClone));
                }
                else
                {
                    result.push_back(WeakIntrusive<TIBase>(toClone));
                }
            }
            return result;
        };
        constexpr int loopIters = 2 * 1024;
        constexpr int numThreads = 16;
        std::vector<SharedIntrusive<TIBase>> toClone;
        std::barrier loopStartSyncPoint{numThreads};
        std::barrier postCreateToCloneSyncPoint{numThreads};
        std::barrier postCreateVecOfPointersSyncPoint{numThreads};
        auto engines = [&]() -> std::vector<std::default_random_engine> {
            std::random_device rd;
            std::vector<std::default_random_engine> result;
            result.reserve(numThreads);
            for (int i = 0; i < numThreads; ++i)
                result.emplace_back(rd());
            return result;
        }();

        // cloneAndDestroy clones the strong pointer into a vector of mixed
        // strong and weak pointers and destroys them all at once.
        // threadId==0 is special.
        auto cloneAndDestroy = [&](int threadId) {
            for (int i = 0; i < loopIters; ++i)
            {
                // ------ Sync Point ------
                loopStartSyncPoint.arrive_and_wait();

                // only thread 0 should reset the state
                std::optional<TIBase::ResetStatesGuard> rsg;
                if (threadId == 0)
                {
                    // Thread 0 is the genesis thread. It creates the strong
                    // pointers to be cloned by the other threads. This
                    // thread will also check that the destructor ran and
                    // clear the temporary variables.

                    rsg.emplace(false);
                    auto [destructorRan, partialDeleteRan] =
                        getDestructorState();
                    BEAST_EXPECT(!i || destructorRan);
                    destructionState.store(0, std::memory_order_release);

                    toClone.clear();
                    toClone.resize(numThreads);
                    auto strong = make_SharedIntrusive<TIBase>();
                    strong->tracingCallback_ = tracingCallback;
                    std::fill(toClone.begin(), toClone.end(), strong);
                }

                // ------ Sync Point ------
                postCreateToCloneSyncPoint.arrive_and_wait();

                auto v =
                    createVecOfPointers(toClone[threadId], engines[threadId]);
                toClone[threadId].reset();

                // ------ Sync Point ------
                postCreateVecOfPointersSyncPoint.arrive_and_wait();

                v.clear();
            }
        };
        std::vector<std::jthread> threads;
        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back(cloneAndDestroy, i);
        }
        for (int i = 0; i < numThreads; ++i)
        {
            threads[i].join();
        }
    }

    void
    testMultithreadedClearMixedUnion()
    {
        testcase("Multithreaded Clear Mixed Union");

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

        TIBase::ResetStatesGuard rsg{true};

        std::atomic<int> destructionState{0};
        // returns destructorRan and partialDestructorRan (in that order)
        auto getDestructorState = [&]() -> std::pair<bool, bool> {
            int s = destructionState.load(std::memory_order_relaxed);
            return {(s & 1) != 0, (s & 2) != 0};
        };
        auto setDestructorRan = [&]() -> void {
            destructionState.fetch_or(1, std::memory_order_acq_rel);
        };
        auto setPartialDeleteRan = [&]() -> void {
            destructionState.fetch_or(2, std::memory_order_acq_rel);
        };
        auto tracingCallback = [&](TrackedState cur,
                                   std::optional<TrackedState> next) {
            using enum TrackedState;
            auto [destructorRan, partialDeleteRan] = getDestructorState();
            if (next == partiallyDeleted)
            {
                BEAST_EXPECT(!partialDeleteRan && !destructorRan);
                setPartialDeleteRan();
            }
            if (next == deleted)
            {
                BEAST_EXPECT(!destructorRan);
                setDestructorRan();
            }
        };
        auto createVecOfPointers = [&](auto const& toClone,
                                       std::default_random_engine& eng)
            -> std::vector<SharedWeakUnion<TIBase>> {
            std::vector<SharedWeakUnion<TIBase>> result;
            std::uniform_int_distribution<> toCreateDist(4, 64);
            auto numToCreate = toCreateDist(eng);
            result.reserve(numToCreate);
            for (int i = 0; i < numToCreate; ++i)
                result.push_back(SharedIntrusive<TIBase>(toClone));
            return result;
        };
        constexpr int loopIters = 2 * 1024;
        constexpr int flipPointersLoopIters = 256;
        constexpr int numThreads = 16;
        std::vector<SharedIntrusive<TIBase>> toClone;
        std::barrier loopStartSyncPoint{numThreads};
        std::barrier postCreateToCloneSyncPoint{numThreads};
        std::barrier postCreateVecOfPointersSyncPoint{numThreads};
        std::barrier postFlipPointersLoopSyncPoint{numThreads};
        auto engines = [&]() -> std::vector<std::default_random_engine> {
            std::random_device rd;
            std::vector<std::default_random_engine> result;
            result.reserve(numThreads);
            for (int i = 0; i < numThreads; ++i)
                result.emplace_back(rd());
            return result;
        }();

        // cloneAndDestroy clones the strong pointer into a vector of
        // mixed strong and weak pointers, runs a loop that randomly
        // changes strong pointers to weak pointers,  and destroys them
        // all at once.
        auto cloneAndDestroy = [&](int threadId) {
            for (int i = 0; i < loopIters; ++i)
            {
                // ------ Sync Point ------
                loopStartSyncPoint.arrive_and_wait();

                // only thread 0 should reset the state
                std::optional<TIBase::ResetStatesGuard> rsg;
                if (threadId == 0)
                {
                    // threadId 0 is the genesis thread. It creates the
                    // strong point to be cloned by the other threads. This
                    // thread will also check that the destructor ran and
                    // clear the temporary variables.
                    rsg.emplace(false);
                    auto [destructorRan, partialDeleteRan] =
                        getDestructorState();
                    BEAST_EXPECT(!i || destructorRan);
                    destructionState.store(0, std::memory_order_release);

                    toClone.clear();
                    toClone.resize(numThreads);
                    auto strong = make_SharedIntrusive<TIBase>();
                    strong->tracingCallback_ = tracingCallback;
                    std::fill(toClone.begin(), toClone.end(), strong);
                }

                // ------ Sync Point ------
                postCreateToCloneSyncPoint.arrive_and_wait();

                auto v =
                    createVecOfPointers(toClone[threadId], engines[threadId]);
                toClone[threadId].reset();

                // ------ Sync Point ------
                postCreateVecOfPointersSyncPoint.arrive_and_wait();

                std::uniform_int_distribution<> isStrongDist(0, 1);
                for (int f = 0; f < flipPointersLoopIters; ++f)
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
                postFlipPointersLoopSyncPoint.arrive_and_wait();

                v.clear();
            }
        };
        std::vector<std::jthread> threads;
        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back(cloneAndDestroy, i);
        }
        for (int i = 0; i < numThreads; ++i)
        {
            threads[i].join();
        }
    }

    void
    testMultithreadedLockingWeak()
    {
        testcase("Multithreaded Locking Weak");

        // This test creates a single shared atomic pointer that multiple thread
        // create weak pointers from. The threads then lock the weak pointers.
        // Both threads clear all the pointers and check that the invariants
        // hold.

        using enum TrackedState;

        TIBase::ResetStatesGuard rsg{true};

        std::atomic<int> destructionState{0};
        // returns destructorRan and partialDestructorRan (in that order)
        auto getDestructorState = [&]() -> std::pair<bool, bool> {
            int s = destructionState.load(std::memory_order_relaxed);
            return {(s & 1) != 0, (s & 2) != 0};
        };
        auto setDestructorRan = [&]() -> void {
            destructionState.fetch_or(1, std::memory_order_acq_rel);
        };
        auto setPartialDeleteRan = [&]() -> void {
            destructionState.fetch_or(2, std::memory_order_acq_rel);
        };
        auto tracingCallback = [&](TrackedState cur,
                                   std::optional<TrackedState> next) {
            using enum TrackedState;
            auto [destructorRan, partialDeleteRan] = getDestructorState();
            if (next == partiallyDeleted)
            {
                BEAST_EXPECT(!partialDeleteRan && !destructorRan);
                setPartialDeleteRan();
            }
            if (next == deleted)
            {
                BEAST_EXPECT(!destructorRan);
                setDestructorRan();
            }
        };

        constexpr int loopIters = 2 * 1024;
        constexpr int lockWeakLoopIters = 256;
        constexpr int numThreads = 16;
        std::vector<SharedIntrusive<TIBase>> toLock;
        std::barrier loopStartSyncPoint{numThreads};
        std::barrier postCreateToLockSyncPoint{numThreads};
        std::barrier postLockWeakLoopSyncPoint{numThreads};

        // lockAndDestroy creates weak pointers from the strong pointer
        // and runs a loop that locks the weak pointer. At the end of the loop
        // all the pointers are destroyed all at once.
        auto lockAndDestroy = [&](int threadId) {
            for (int i = 0; i < loopIters; ++i)
            {
                // ------ Sync Point ------
                loopStartSyncPoint.arrive_and_wait();

                // only thread 0 should reset the state
                std::optional<TIBase::ResetStatesGuard> rsg;
                if (threadId == 0)
                {
                    // threadId 0 is the genesis thread. It creates the
                    // strong point to be locked by the other threads. This
                    // thread will also check that the destructor ran and
                    // clear the temporary variables.
                    rsg.emplace(false);
                    auto [destructorRan, partialDeleteRan] =
                        getDestructorState();
                    BEAST_EXPECT(!i || destructorRan);
                    destructionState.store(0, std::memory_order_release);

                    toLock.clear();
                    toLock.resize(numThreads);
                    auto strong = make_SharedIntrusive<TIBase>();
                    strong->tracingCallback_ = tracingCallback;
                    std::fill(toLock.begin(), toLock.end(), strong);
                }

                // ------ Sync Point ------
                postCreateToLockSyncPoint.arrive_and_wait();

                // Multiple threads all create a weak pointer from the same
                // strong pointer
                WeakIntrusive weak{toLock[threadId]};
                for (int wi = 0; wi < lockWeakLoopIters; ++wi)
                {
                    BEAST_EXPECT(!weak.expired());
                    auto strong = weak.lock();
                    BEAST_EXPECT(strong);
                }

                // ------ Sync Point ------
                postLockWeakLoopSyncPoint.arrive_and_wait();

                toLock[threadId].reset();
            }
        };
        std::vector<std::jthread> threads;
        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back(lockAndDestroy, i);
        }
        for (int i = 0; i < numThreads; ++i)
        {
            threads[i].join();
        }
    }

    void
    run() override
    {
        testBasics();
        testPartialDelete();
        testDestructor();
        testMultithreadedClearMixedVariant();
        testMultithreadedClearMixedUnion();
        testMultithreadedLockingWeak();
    }
};  // namespace tests

BEAST_DEFINE_TESTSUITE(IntrusiveShared, ripple_basics, ripple);
}  // namespace tests
}  // namespace ripple
