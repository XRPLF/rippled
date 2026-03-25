#include <test/jtx.h>

#include <xrpl/core/JobQueue.h>
#include <xrpl/core/JobQueueAwaiter.h>

#include <chrono>
#include <mutex>

namespace xrpl {
namespace test {

/**
 * Test suite for the C++20 coroutine primitives: CoroTask, CoroTaskRunner,
 * and JobQueueAwaiter.
 *
 * Dependency Diagram
 * ==================
 *
 *   CoroTask_test
 *   +-------------------------------------------------+
 *   | + gate (inner class) : condition_variable helper |
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
 *   testJobQueueAwaiter       | JobQueueAwaiter suspend + auto-repost
 *   testThreadSpecificStorage | LocalValue isolation across coroutines
 *   testExceptionPropagation  | unhandled_exception() in promise_type
 *   testMultipleYields        | N sequential suspend/resume cycles
 *   testValueReturn           | CoroTask<T> co_return value
 *   testValueException        | CoroTask<T> exception via co_await
 *   testValueChaining         | nested CoroTask<T> -> CoroTask<T>
 *   testShutdownRejection     | postCoroTask returns nullptr when stopping
 *   testExpectEarlyExit       | expectEarlyExit() with finished_ == false
 */
class CoroTask_test : public beast::unit_test::suite
{
public:
    /**
     * Simple one-shot gate for synchronizing between test thread
     * and coroutine worker threads. signal() sets the flag;
     * wait_for() blocks until signaled or timeout.
     */
    class gate
    {
    private:
        std::condition_variable cv_;
        std::mutex mutex_;
        bool signaled_ = false;

    public:
        /**
         * Block until signaled or timeout expires.
         *
         * @param rel_time Maximum duration to wait
         *
         * @return true if signaled before timeout
         */
        template <class Rep, class Period>
        bool
        wait_for(std::chrono::duration<Rep, Period> const& rel_time)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, rel_time, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        /**
         * Signal the gate, waking any waiting thread.
         */
        void
        signal()
        {
            std::lock_guard lk(mutex_);
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [gp = &g](auto) -> CoroTask<void> {
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        BEAST_EXPECT(g.wait_for(5s));
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g1, g2;
        std::shared_ptr<JobQueue::CoroTaskRunner> r;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT,
            "CoroTaskTest",
            [rp = &r, g1p = &g1, g2p = &g2](auto runner) -> CoroTask<void> {
                *rp = runner;
                g1p->signal();
                co_await runner->suspend();
                g2p->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        BEAST_EXPECT(g1.wait_for(5s));
        runner->join();
        runner->post();
        BEAST_EXPECT(g2.wait_for(5s));
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [gp = &g](auto runner) -> CoroTask<void> {
                runner->post();
                co_await runner->suspend();
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(g.wait_for(5s));
    }

    /**
     * JobQueueAwaiter suspend + auto-repost across multiple yield points.
     */
    void
    testJobQueueAwaiter()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("JobQueueAwaiter");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        int step = 0;
        env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [sp = &step, gp = &g](auto runner) -> CoroTask<void> {
                *sp = 1;
                co_await runner->yieldAndPost();
                *sp = 2;
                co_await runner->yieldAndPost();
                *sp = 3;
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(step == 3);
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

        static int const N = 4;
        std::array<std::shared_ptr<JobQueue::CoroTaskRunner>, N> a;

        LocalValue<int> lv(-1);
        BEAST_EXPECT(*lv == -1);

        gate g;
        jq.addJob(jtCLIENT, "LocalValTest", [&]() {
            this->BEAST_EXPECT(*lv == -1);
            *lv = -2;
            this->BEAST_EXPECT(*lv == -2);
            g.signal();
        });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(*lv == -1);

        for (int i = 0; i < N; ++i)
        {
            jq.postCoroTask(
                jtCLIENT,
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
            BEAST_EXPECT(g.wait_for(5s));
            a[i]->join();
        }
        for (auto const& r : a)
        {
            r->post();
            BEAST_EXPECT(g.wait_for(5s));
            r->join();
        }
        for (auto const& r : a)
        {
            r->post();
            r->join();
        }

        jq.addJob(jtCLIENT, "LocalValTest", [&]() {
            this->BEAST_EXPECT(*lv == -2);
            g.signal();
        });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(*lv == -1);
    }

    /**
     * Exception thrown in coroutine body is caught by
     * promise_type::unhandled_exception(). Coroutine completes.
     */
    void
    testExceptionPropagation()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("exception propagation");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [gp = &g](auto) -> CoroTask<void> {
                gp->signal();
                throw std::runtime_error("test exception");
                co_return;
            });
        BEAST_EXPECT(runner);
        BEAST_EXPECT(g.wait_for(5s));
        runner->join();
        // The exception is caught by promise_type::unhandled_exception()
        // and the coroutine is considered done
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        int counter = 0;
        std::shared_ptr<JobQueue::CoroTaskRunner> r;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT,
            "CoroTaskTest",
            [rp = &r, cp = &counter, gp = &g](auto runner) -> CoroTask<void> {
                *rp = runner;
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

        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(counter == 1);
        runner->join();

        runner->post();
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(counter == 2);
        runner->join();

        runner->post();
        BEAST_EXPECT(g.wait_for(5s));
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        int result = 0;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [rp = &result, gp = &g](auto) -> CoroTask<void> {
                auto inner = []() -> CoroTask<int> { co_return 42; };
                *rp = co_await inner();
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        BEAST_EXPECT(g.wait_for(5s));
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        bool caught = false;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [cp = &caught, gp = &g](auto) -> CoroTask<void> {
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
                    *cp = true;
                }
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        BEAST_EXPECT(g.wait_for(5s));
        runner->join();
        BEAST_EXPECT(caught);
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g;
        int result = 0;
        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [rp = &result, gp = &g](auto) -> CoroTask<void> {
                auto add = [](int a, int b) -> CoroTask<int> { co_return a + b; };
                auto mul = [add](int a, int b) -> CoroTask<int> {
                    int sum = co_await add(a, b);
                    co_return sum * 2;
                };
                *rp = co_await mul(3, 4);
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(runner);
        BEAST_EXPECT(g.wait_for(5s));
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
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        // Stop the JobQueue
        env.app().getJobQueue().stop();

        auto runner = env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTaskTest", [](auto) -> CoroTask<void> { co_return; });
        BEAST_EXPECT(!runner);
    }

    /**
     * Exercises expectEarlyExit() when the coroutine has never run
     * (finished_ is false). This covers the if-body that decrements
     * nSuspend_ and sets finished_ = true.
     */
    void
    testExpectEarlyExit()
    {
        using namespace jtx;

        testcase("expectEarlyExit with unfinished coroutine");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        auto& jq = env.app().getJobQueue();

        // Create a runner directly (bypassing postCoroTask) so we can
        // control the lifecycle and exercise the early-exit path.
        auto runner = std::make_shared<JobQueue::CoroTaskRunner>(
            JobQueue::CoroTaskRunner::create_t{}, jq, jtCLIENT, "TestEarlyExit");
        runner->init([](auto) -> CoroTask<void> { co_return; });

        // Simulate the nSuspend_ increment that postCoroTask normally does.
        runner->onSuspend();

        // expectEarlyExit: finished_ is false, so the if-body runs
        // (decrements nSuspend_, sets finished_ = true, destroys frame).
        runner->expectEarlyExit();

        BEAST_EXPECT(!runner->runnable());
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
        testExpectEarlyExit();
    }
};

BEAST_DEFINE_TESTSUITE(CoroTask, core, xrpl);

}  // namespace test
}  // namespace xrpl
