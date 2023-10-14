#include <ripple/basics/IntrusivePointer.ipp>
#include <ripple/basics/IntrusiveRefCounts.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>
#include <test/unit_test/SuiteJournal.h>

#include <atomic>
#include <string>

// TODO: rename this test. It tests intrusive pointers

namespace ripple {
namespace tests {

namespace {
enum class TrackedState { alive, partiallyDeleted, deleted };

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
        state[id_] = TrackedState::deleted;
    }
    void
    partialDestructor()
    {
        state[id_] = TrackedState::partiallyDeleted;
    }

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
    testIt(beast::Journal const& journal)
    {
        {
            TIBase b;
            b.addStrongRef();
            BEAST_EXPECT(b.use_count() == 1);
            b.addWeakRef();
            BEAST_EXPECT(b.use_count() == 1);
            auto a = b.releaseStrongRef();
            BEAST_EXPECT(a == ReleaseRefAction::partialDestroy);
            BEAST_EXPECT(b.use_count() == 0);
            partialDestructorFinished(&b);
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
    run() override
    {
        using namespace beast::severities;
        test::SuiteJournal journal("IntrusiveShared_test", *this);

        testIt(journal);
    }
};

BEAST_DEFINE_TESTSUITE(IntrusiveShared, ripple_basics, ripple);
}  // namespace tests
}  // namespace ripple
