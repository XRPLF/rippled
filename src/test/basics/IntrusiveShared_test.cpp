#include <ripple/basics/IntrusivePointer.ipp>
#include <ripple/basics/IntrusiveRefCounts.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>
#include <test/unit_test/SuiteJournal.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <string>
#include <thread>

namespace ripple {
namespace tests {

namespace {
enum class TrackedState {
    alive,
    partiallyDeletedStarted,
    partiallyDeleted,
    deletedStarted,
    deleted
};

class TIBase : public IntrusiveRefCounts
{
public:
    static std::vector<TrackedState> state;
    static TrackedState
    getState(int id)
    {
        assert(id < state.size());
        return state[id];
    }

    TIBase() : id_{checkoutID()}
    {
        assert(state.size() == id_);
        state.push_back(TrackedState::alive);
    }
    ~TIBase()
    {
        tracingCallback_(state[id_], TrackedState::deletedStarted);

        state[id_] = TrackedState::deletedStarted;

        tracingCallback_(state[id_], TrackedState::deleted);

        state[id_] = TrackedState::deleted;

        tracingCallback_(state[id_], std::nullopt);
    }

    void
    partialDestructor()
    {
        tracingCallback_(state[id_], TrackedState::partiallyDeletedStarted);

        state[id_] = TrackedState::partiallyDeletedStarted;

        tracingCallback_(state[id_], TrackedState::partiallyDeleted);

        state[id_] = TrackedState::partiallyDeleted;

        tracingCallback_(state[id_], std::nullopt);
    }

    std::function<void(TrackedState, std::optional<TrackedState>)>
        tracingCallback_ = [](TrackedState, std::optional<TrackedState>) {};

    int id_;

private:
    static int
    checkoutID()
    {
        static int id = 0;
        return id++;
    }
};

std::vector<TrackedState> TIBase::state;

class TIDerived : public TIBase
{
public:
    TIDerived()
    {
    }
};
}  // namespace

class IntrusiveShared_test : public beast::unit_test::suite
{
public:
    void
    testEasy(beast::Journal const& journal)
    {
        {
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

        std::vector<SharedIntrusive<TIBase, false>> strong;
        std::vector<WeakIntrusive<TIBase>> weak;
        {
            using enum TrackedState;
            auto b = make_SharedIntrusive<TIBase, false>();
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

            b = make_SharedIntrusive<TIBase, false>();
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
            using enum TrackedState;
            auto b = make_SharedIntrusive<TIBase, false>();
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
            s = w.lock();
            BEAST_EXPECT(!s);
            w.reset();
            BEAST_EXPECT(TIBase::getState(id) == deleted);
        }
    }

    void
    testThreads(beast::Journal const& journal)
    {
        using enum TrackedState;

        {
            // This test creates two threads. One with a strong pointer and one
            // with a weak pointer. We let the weakpointer trigger
            auto b = make_SharedIntrusive<TIBase, false>();
            bool destructorRan = false;
            bool partialDeleteRan = false;
            std::latch weakCreateLatch{2};
            std::latch partialDeleteStartedLatch{2};
            b->tracingCallback_ = [&](TrackedState cur,
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
                    partialDeleteStartedLatch.arrive_and_wait();
                    using namespace std::chrono_literals;
                    // Sleep and let the weak pointer go out of scope,
                    // potentially triggering a destructor while partial delete
                    // is running. The test is to make sure that doesn't happen.
                    std::this_thread::sleep_for(800ms);
                }
                if (cur == partiallyDeleted)
                    partialDeleteRan = true;
                if (cur == deleted)
                    destructorRan = true;
            };
            std::jthread t1{[&] {
                WeakIntrusive<TIBase> w{b};
                weakCreateLatch.arrive_and_wait();
                partialDeleteStartedLatch.arrive_and_wait();
                w.reset();  // Trigger a full delete as soon as the partial
                            // delete starts
            }};
            std::jthread t2{[&] {
                weakCreateLatch.arrive_and_wait();
                b.reset();  // Trigger a partial delete
            }};
            t1.join();
            t2.join();

            BEAST_EXPECT(destructorRan && partialDeleteRan);
        }
    }

    void
    run() override
    {
        using namespace beast::severities;
        test::SuiteJournal journal("IntrusiveShared_test", *this);

        testEasy(journal);
        testThreads(journal);
    }
};

BEAST_DEFINE_TESTSUITE(IntrusiveShared, ripple_basics, ripple);
}  // namespace tests
}  // namespace ripple
