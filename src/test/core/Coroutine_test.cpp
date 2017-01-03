//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/core/JobQueue.h>
#include <test/jtx.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ripple {
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
            auto b = cv_.wait_for(lk, rel_time, [=]{ return signaled_; });
            signaled_ = false;
            return b;
        }

        void
        signal()
        {
            std::lock_guard<std::mutex> lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    void
    correct_order()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto& jq = env.app().getJobQueue();
        jq.setThreadCount(0, false);
        gate g1, g2;
        std::shared_ptr<JobQueue::Coro> c;
        jq.postCoro(jtCLIENT, "Coroutine-Test",
            [&](auto const& cr)
            {
                c = cr;
                g1.signal();
                c->yield();
                g2.signal();
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
        Env env(*this);
        auto& jq = env.app().getJobQueue();
        jq.setThreadCount(0, false);
        gate g;
        jq.postCoro(jtCLIENT, "Coroutine-Test",
            [&](auto const& c)
            {
                c->post();
                c->yield();
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
    }

    void
    thread_specific_storage()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto& jq = env.app().getJobQueue();
        jq.setThreadCount(0, true);
        static int const N = 4;
        std::array<std::shared_ptr<JobQueue::Coro>, N> a;

        LocalValue<int> lv(-1);
        BEAST_EXPECT(*lv == -1);

        gate g;
        jq.addJob(jtCLIENT, "LocalValue-Test",
            [&](auto const& job)
            {
                this->BEAST_EXPECT(*lv == -1);
                *lv = -2;
                this->BEAST_EXPECT(*lv == -2);
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(*lv == -1);

        for(int i = 0; i < N; ++i)
        {
            jq.postCoro(jtCLIENT, "Coroutine-Test",
                [&, id = i](auto const& c)
                {
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
            BEAST_EXPECT(g.wait_for(5s));
            a[i]->join();
        }
        for(auto const& c : a)
        {
            c->post();
            BEAST_EXPECT(g.wait_for(5s));
            c->join();
        }
        for(auto const& c : a)
        {
            c->post();
            c->join();
        }

        jq.addJob(jtCLIENT, "LocalValue-Test",
            [&](auto const& job)
            {
                this->BEAST_EXPECT(*lv == -2);
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(*lv == -1);
    }

    void
    run()
    {
        correct_order();
        incorrect_order();
        thread_specific_storage();
    }
};

BEAST_DEFINE_TESTSUITE(Coroutine,core,ripple);

} // test
} // ripple
