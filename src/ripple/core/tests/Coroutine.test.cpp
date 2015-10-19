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
#include <ripple/core/JobCoro.h>
#include <ripple/core/JobQueue.h>
#include <ripple/test/jtx.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ripple {
namespace test {

class Coroutine_test : public beast::unit_test::suite
{
public:
    void
    test_coroutine()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        std::atomic<int> i{0};
        std::condition_variable cv;
        auto& jq = env.app().getJobQueue();
        jq.setThreadCount(0, false);
        jq.postCoro(jtCLIENT, "Coroutine-Test",
            [&](std::shared_ptr<JobCoro> jc)
            {
                std::thread t(
                    [&i, jc]()
                    {
                        std::this_thread::sleep_for(20ms);
                        ++i;
                        jc->post();
                    });
                jc->yield();
                t.join();
                ++i;
                cv.notify_one();
            });

        {
            std::mutex m;
            std::unique_lock<std::mutex> lk(m);
            expect(cv.wait_for(lk, 1s,
                [&]()
                {
                    return i == 2;
                }));
        }
        jq.shutdown();
        expect(i == 2);
    }

    void
    test_incorrect_order()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        std::atomic<int> i{0};
        std::condition_variable cv;
        auto& jq = env.app().getJobQueue();
        jq.setThreadCount(0, false);
        jq.postCoro(jtCLIENT, "Coroutine-Test",
            [&](std::shared_ptr<JobCoro> jc)
            {
                jc->post();
                jc->yield();
                ++i;
                cv.notify_one();
            });

        {
            std::mutex m;
            std::unique_lock<std::mutex> lk(m);
            expect(cv.wait_for(lk, 1s,
                [&]()
                {
                    return i == 1;
                }));
        }
        jq.shutdown();
        expect(i == 1);
    }

    void
    run()
    {
        test_coroutine();
        test_incorrect_order();
    }
};

BEAST_DEFINE_TESTSUITE(Coroutine,core,ripple);

} // test
} // ripple
