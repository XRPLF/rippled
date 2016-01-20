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
#include <ripple/test/jtx.h>
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
        std::shared_ptr<JobCoro> jc;
        jq.postCoro(jtCLIENT, "Coroutine-Test",
            [&](auto const& jcr)
            {
                jc = jcr;
                g1.signal();
                jc->yield();
                g2.signal();
            });
        expect(g1.wait_for(5s));
        jc->join();
        jc->post();
        expect(g2.wait_for(5s));
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
            [&](auto const& jc)
            {
                jc->post();
                jc->yield();
                g.signal();
            });
        expect(g.wait_for(5s));
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
        std::array<std::shared_ptr<JobCoro>, N> a;

        LocalValue<int> lv(-1);
        expect(*lv == -1);

        gate g;
        jq.addJob(jtCLIENT, "LocalValue-Test",
            [&](auto const& job)
            {
                this->expect(*lv == -1);
                *lv = -2;
                this->expect(*lv == -2);
                g.signal();
            });
        expect(g.wait_for(5s));
        expect(*lv == -1);

        for(int i = 0; i < N; ++i)
        {
            jq.postCoro(jtCLIENT, "Coroutine-Test",
                [&, id = i](auto const& jc)
                {
                    a[id] = jc;
                    g.signal();
                    jc->yield();

                    this->expect(*lv == -1);
                    *lv = id;
                    this->expect(*lv == id);
                    g.signal();
                    jc->yield();

                    this->expect(*lv == id);
                });
            expect(g.wait_for(5s));
            a[i]->join();
        }
        for(auto const& jc : a)
        {
            jc->post();
            expect(g.wait_for(5s));
            jc->join();
        }
        for(auto const& jc : a)
        {
            jc->post();
            jc->join();
        }

        jq.addJob(jtCLIENT, "LocalValue-Test",
            [&](auto const& job)
            {
                this->expect(*lv == -2);
                g.signal();
            });
        expect(g.wait_for(5s));
        expect(*lv == -1);
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
