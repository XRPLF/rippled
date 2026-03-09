#include <test/jtx.h>

#include <xrpl/core/JobQueue.h>

#include <chrono>
#include <mutex>

namespace xrpl {
namespace test {

class Coroutine_test : public beast::unit_test::suite
{
public:
    class gate
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
        wait_for(std::chrono::duration<Rep, Period> const& rel_time)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, rel_time, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

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

    void
    correct_order()
    {
        using namespace std::chrono_literals;
        using namespace jtx;

        testcase("correct order");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->FORCE_MULTI_THREAD = true;
            return cfg;
        }));

        gate g1, g2;
        std::shared_ptr<JobQueue::CoroTaskRunner> c;
        env.app().getJobQueue().postCoroTask(
            jtCLIENT, "CoroTest", [cp = &c, g1p = &g1, g2p = &g2](auto runner) -> CoroTask<void> {
                *cp = runner;
                g1p->signal();
                co_await runner->suspend();
                g2p->signal();
                co_return;
            });
        BEAST_EXPECT(g1.wait_for(5s));
        c->join();
        c->post();
        BEAST_EXPECT(g2.wait_for(5s));
    }

    void
    incorrect_order()
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
            jtCLIENT, "CoroTest", [gp = &g](auto runner) -> CoroTask<void> {
                // Schedule a resume before suspending.  The posted job
                // cannot actually call resume() until the current resume()
                // releases CoroTaskRunner::mutex_, which only happens after
                // the coroutine suspends at co_await.
                runner->post();
                co_await runner->suspend();
                gp->signal();
                co_return;
            });
        BEAST_EXPECT(g.wait_for(5s));
    }

    void
    thread_specific_storage()
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
            BEAST_EXPECT(g.wait_for(5s));
            a[i]->join();
        }
        for (auto const& c : a)
        {
            c->post();
            BEAST_EXPECT(g.wait_for(5s));
            c->join();
        }
        for (auto const& c : a)
        {
            c->post();
            c->join();
        }

        jq.addJob(jtCLIENT, "LocalValTest", [&]() {
            this->BEAST_EXPECT(*lv == -2);
            g.signal();
        });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(*lv == -1);
    }

    void
    run() override
    {
        correct_order();
        incorrect_order();
        thread_specific_storage();
    }
};

BEAST_DEFINE_TESTSUITE(Coroutine, core, xrpl);

}  // namespace test
}  // namespace xrpl
