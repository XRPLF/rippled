#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/LocalValue.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/CoroTask.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/JobQueueAwaiter.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// Tests intentionally capture state in coroutine lambdas; lifetimes are
// controlled by Gate synchronization and join() before scope exit.
// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines)

namespace xrpl::test {

/**
 * Test suite for the C++20 coroutine primitives: CoroTask, CoroTaskRunner,
 * and JobQueueAwaiter.
 *
 * Dependency Diagram
 * ==================
 *
 *   CoroTask_test
 *   +-------------------------------------------------+
 *   | + Gate (inner class) : condition_variable helper |
 *   +-------------------------------------------------+
 *          |  uses
 *          v
 *   jtx::Env  -->  JobQueue::postCoroTask()
 *                       |
 *                       +-- CoroTaskRunner (suspend / post / resume)
 *                       +-- CoroTask<void> / CoroTask<T>
 *                       +-- JobQueueAwaiter
 *
 * Test Coverage Matrix
 * ====================
 *
 *   Test                      | Primitives exercised
 *   --------------------------+----------------------------------------------
 *   testVoidCompletion        | CoroTask<void> basic lifecycle
 *   testCorrectOrder          | suspend() -> join() -> post() -> complete
 *   testIncorrectOrder        | post() before suspend() (race-safe path)
 *   testJobQueueAwaiter       | JobQueueAwaiter + yieldAndPost suspend/repost
 *   testThreadSpecificStorage | LocalValue isolation across coroutines
 *   testExceptionPropagation  | CoroTask<void> exception via co_await
 *   testMultipleYields        | N sequential suspend/resume cycles
 *   testValueReturn           | CoroTask<T> co_return value
 *   testValueException        | CoroTask<T> exception via co_await
 *   testValueChaining         | nested CoroTask<T> -> CoroTask<T>
 *   testShutdownRejection     | postCoroTask returns nullptr when stopping
 */
class CoroTask_test : public beast::unit_test::Suite
{
public:
    /**
     * Simple one-shot gate for synchronizing between test thread
     * and coroutine worker threads. signal() sets the flag;
     * waitFor() blocks until signaled or timeout.
     */
    class Gate
    {
    private:
        std::condition_variable cv_;
        std::mutex mutex_;
        bool signaled_ = false;

    public:
        /**
         * Block until signaled or timeout expires.
         *
         * @param relTime Maximum duration to wait
         *
         * @return true if signaled before timeout
         */
        template <class Rep, class Period>
        bool
        waitFor(std::chrono::duration<Rep, Period> const& relTime)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, relTime, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        /**
         * Signal the gate, waking any waiting thread.
         */
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

    /**
     * CoroTask<void> runs to completion and runner becomes non-runnable.
     */
    void
    testVoidCompletion()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("void completion");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [gp = &g](auto) -> CoroTask<void> {
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        runner->join();
        BEAST_EXPECT(!runner->runnable());
    }

    /**
     * Correct order: suspend, join, post, complete.
     * Mirrors existing Coroutine_test::correct_order.
     */
    void
    testCorrectOrder()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("correct order");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g1, g2;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [g1p = &g1, g2p = &g2](auto runner) -> CoroTask<void> {
                g1p->signal();
                co_await runner->suspend();
                g2p->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g1.waitFor(5s)))
            return;
        runner->join();
        runner->post();
        if (!BEAST_EXPECT(g2.waitFor(5s)))
            return;
        runner->join();
    }

    /**
     * Incorrect order: post() before suspend(). Verifies the
     * race-safe path. Mirrors Coroutine_test::incorrect_order.
     */
    void
    testIncorrectOrder()
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
            JtClient, "CoroTaskTest", [gp = &g](auto runner) -> CoroTask<void> {
                runner->post();
                co_await runner->suspend();
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(g.waitFor(5s));
    }

    /**
     * Suspend + auto-repost across multiple yield points, using the
     * external JobQueueAwaiter struct for the first suspension and the
     * inline yieldAndPost() awaiter for the second. JobQueueAwaiter is
     * used at only one co_await point per coroutine (see the GCC-12
     * multi-use warning in JobQueueAwaiter.h).
     */
    void
    testJobQueueAwaiter()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("JobQueueAwaiter");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        std::vector<int> steps;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [sp = &steps, gp = &g](auto runner) -> CoroTask<void> {
                sp->push_back(1);
                co_await JobQueueAwaiter{runner};
                sp->push_back(2);
                co_await runner->yieldAndPost();
                sp->push_back(3);
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        runner->join();
        BEAST_EXPECT(steps == std::vector<int>({1, 2, 3}));
    }

    /**
     * Per-coroutine LocalValue isolation. Each coroutine sees its own
     * copy of thread-local state. Mirrors Coroutine_test::thread_specific_storage.
     */
    void
    testThreadSpecificStorage()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("thread specific storage");
        Env env(*this);

        auto& jq = env.app().getJobQueue();

        static constexpr int kN = 4;
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
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        BEAST_EXPECT(*lv == -1);

        for (int i = 0; i < kN; ++i)
        {
            jq.postCoroTask(
                JtClient,
                "CoroTaskTest",
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
        for (auto const& r : a)
        {
            r->post();
            if (!BEAST_EXPECT(g.waitFor(5s)))
                return;
            r->join();
        }
        for (auto const& r : a)
        {
            r->post();
            r->join();
        }

        jq.addJob(JtClient, "LocalValTest", [&]() {
            this->BEAST_EXPECT(*lv == -2);
            g.signal();
        });
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        BEAST_EXPECT(*lv == -1);
    }

    /**
     * An exception thrown in an awaited CoroTask<void> is rethrown into
     * the awaiting coroutine by await_resume(), with the original
     * message intact. (An exception escaping the top-level body has no
     * awaiter to rethrow it; it is captured by unhandled_exception()
     * and logged by CoroTaskRunner::resume().)
     */
    void
    testExceptionPropagation()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("exception propagation");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        std::string what;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [wp = &what, gp = &g](auto) -> CoroTask<void> {
                auto inner = []() -> CoroTask<void> {
                    throw std::runtime_error("test exception");
                    co_return;
                };
                try
                {
                    co_await inner();
                }
                catch (std::runtime_error const& e)
                {
                    *wp = e.what();
                }
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        runner->join();
        BEAST_EXPECT(what == "test exception");
        BEAST_EXPECT(!runner->runnable());
    }

    /**
     * Multiple sequential suspend/resume cycles via co_await.
     */
    void
    testMultipleYields()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("multiple yields");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        int counter = 0;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [cp = &counter, gp = &g](auto runner) -> CoroTask<void> {
                ++(*cp);
                gp->signal();
                co_await runner->suspend();
                ++(*cp);
                gp->signal();
                co_await runner->suspend();
                ++(*cp);
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);

        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        BEAST_EXPECT(counter == 1);
        runner->join();

        runner->post();
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        BEAST_EXPECT(counter == 2);
        runner->join();

        runner->post();
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        BEAST_EXPECT(counter == 3);
        runner->join();
        BEAST_EXPECT(!runner->runnable());
    }

    /**
     * CoroTask<T> returns a value via co_return. Outer coroutine
     * extracts it with co_await.
     */
    void
    testValueReturn()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("value return");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        int result = 0;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [rp = &result, gp = &g](auto) -> CoroTask<void> {
                auto inner = []() -> CoroTask<int> { co_return 42; };
                *rp = co_await inner();
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        runner->join();
        BEAST_EXPECT(result == 42);
        BEAST_EXPECT(!runner->runnable());
    }

    /**
     * CoroTask<T> propagates exceptions from inner coroutines.
     * Outer coroutine catches via try/catch around co_await.
     */
    void
    testValueException()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("value exception");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        std::string what;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [wp = &what, gp = &g](auto) -> CoroTask<void> {
                auto inner = []() -> CoroTask<int> {
                    throw std::runtime_error("inner error");
                    co_return 0;
                };
                try
                {
                    co_await inner();
                }
                catch (std::runtime_error const& e)
                {
                    *wp = e.what();
                }
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        runner->join();
        BEAST_EXPECT(what == "inner error");
        BEAST_EXPECT(!runner->runnable());
    }

    /**
     * CoroTask<T> chaining. Nested value-returning coroutines
     * compose via co_await.
     */
    void
    testValueChaining()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("value chaining");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        Gate g;
        int result = 0;
        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [rp = &result, gp = &g](auto) -> CoroTask<void> {
                auto add = [](int a, int b) -> CoroTask<int> { co_return a + b; };
                auto mul = [add](int a, int b) -> CoroTask<int> {
                    int const sum = co_await add(a, b);
                    co_return sum * 2;
                };
                *rp = co_await mul(3, 4);
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        if (!BEAST_EXPECT(g.waitFor(5s)))
            return;
        runner->join();
        BEAST_EXPECT(result == 14);  // (3 + 4) * 2
        BEAST_EXPECT(!runner->runnable());
    }

    /**
     * postCoroTask returns nullptr when JobQueue is stopping.
     */
    void
    testShutdownRejection()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("shutdown rejection");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        // Stop the JobQueue
        env.app().getJobQueue().stop();

        auto runner = env.app().getJobQueue().postCoroTask(
            JtClient, "CoroTaskTest", [](auto) -> CoroTask<void> { co_return; });
        BEAST_EXPECT(!runner);
    }

    void
    run() override
    {
        testVoidCompletion();
        testCorrectOrder();
        testIncorrectOrder();
        testJobQueueAwaiter();
        testThreadSpecificStorage();
        testExceptionPropagation();
        testMultipleYields();
        testValueReturn();
        testValueException();
        testValueChaining();
        testShutdownRejection();
    }
};

BEAST_DEFINE_TESTSUITE(CoroTask, core, xrpl);

}  // namespace xrpl::test

// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)
