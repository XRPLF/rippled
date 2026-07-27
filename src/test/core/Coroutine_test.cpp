
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/LocalValue.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/CoroTask.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace xrpl::test {

class Coroutine_test : public beast::unit_test::Suite
{
public:
    class Gate
    {
    private:
        std::condition_variable cv_;
        std::mutex mutex_;
        bool signaled_ = false;

    public:
        // Thread safe, blocks until signaled or period expires.
        // Returns `true` if signaled.
        template <class Rep, class Period>
        bool
        waitFor(std::chrono::duration<Rep, Period> const& relTime)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, relTime, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        void
        signal()
        {
            std::scoped_lock const lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    // NOTE: All coroutine lambdas passed to postCoroTask use explicit
    // pointer-by-value captures instead of [&] to work around a GCC 14
    // bug where reference captures in coroutine lambdas are corrupted
    // in the coroutine frame.

    void
    correctOrder()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("correct order");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g1, g2;
        std::shared_ptr<JobQueue::CoroTaskRunner> c;
        env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTest", [cp = &c, g1p = &g1, g2p = &g2](auto runner) -> CoroTask<void> {
                *cp = runner;
                g1p->signal();
                co_await runner->suspend();
                g2p->signal();
                co_return;
            });
        if (!BEAST_EXPECT(g1.waitFor(5s)))
            return;
        c->join();
        c->post();
        BEAST_EXPECT(g2.waitFor(5s));
    }

    void
    incorrectOrder()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("incorrect order");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTest", [gp = &g](auto runner) -> CoroTask<void> {
                // Schedule a resume before suspending.  The posted job
                // cannot actually call resume() until the current resume()
                // releases CoroTaskRunner::mutex_, which only happens after
                // the coroutine suspends at co_await.
                runner->post();
                co_await runner->suspend();
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(g.waitFor(5s));
    }

    void
    threadSpecificStorage()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("thread specific storage");
        Env env(*this);

        auto& jq = env.app().getJobQueue();

        static int const kN = 4;
        std::array<std::shared_ptr<JobQueue::CoroTaskRunner>, kN> a;

        LocalValue<int> lv(-1);
        BEAST_EXPECT(*lv == -1);

        Gate g;
        jq.addJob(JtClient, "LocalValTest", [&]() {
            this->BEAST_EXPECT(*lv == -1);
            *lv = -2;
            this->BEAST_EXPECT(*lv == -2);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(*lv == -1);

        for (int i = 0; i < kN; ++i)
        {
            jq.postCoroTask(
                JtClient,
                "CoroTest",
                [this, ap = &a, gp = &g, lvp = &lv, id = i](auto runner) -> CoroTask<void> {
                    (*ap)[id] = runner;
                    gp->signal();
                    co_await runner->suspend();

                    this->BEAST_EXPECT(**lvp == -1);
                    **lvp = id;
                    this->BEAST_EXPECT(**lvp == id);
                    gp->signal();
                    co_await runner->suspend();

                    this->BEAST_EXPECT(**lvp == id);
                    co_return;
                });
            if (!BEAST_EXPECT(g.waitFor(5s)))
                return;
            a[i]->join();
        }
        for (auto const& c : a)
        {
            c->post();
            if (!BEAST_EXPECT(g.waitFor(5s)))
                return;
            c->join();
        }
        for (auto const& c : a)
        {
            c->post();
            c->join();
        }

        jq.addJob(JtClient, "LocalValTest", [&]() {
            this->BEAST_EXPECT(*lv == -2);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(*lv == -1);
    }

    void
    run() override
    {
        correctOrder();
        incorrectOrder();
        threadSpecificStorage();
    }
};

BEAST_DEFINE_TESTSUITE(Coroutine, core, xrpl);

}  // namespace xrpl::test
