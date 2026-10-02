
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/LocalValue.h>
#include <xrpl/beast/unit_test/suite.h>
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
        std::shared_ptr<JobQueue::Coro> c;
        env.app().getJobQueue().postCoro(JtClient, "CoroTest", [&](auto const& cr) {
            c = cr;
            g1.signal();
            c->yield();
            g2.signal();
        });
        BEAST_EXPECT(g1.waitFor(5s));
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
        env.app().getJobQueue().postCoro(JtClient, "CoroTest", [&](auto const& c) {
            c->post();
            c->yield();
            g.signal();
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
        std::array<std::shared_ptr<JobQueue::Coro>, kN> a;

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
            jq.postCoro(JtClient, "CoroTest", [&, id = i](auto const& c) {
                a[id] = c;
                g.signal();
                c->yield();

                this->BEAST_EXPECT(*lv == -1);
                *lv = id;
                this->BEAST_EXPECT(*lv == id);
                g.signal();
                c->yield();

                this->BEAST_EXPECT(*lv == id);
            });
            BEAST_EXPECT(g.waitFor(5s));
            a[i]->join();
        }
        for (auto const& c : a)
        {
            c->post();
            BEAST_EXPECT(g.waitFor(5s));
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
