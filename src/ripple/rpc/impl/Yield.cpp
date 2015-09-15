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
#include <ripple/app/main/Application.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/rpc/Yield.h>
#include <ripple/rpc/tests/TestOutputSuite.test.h>

namespace ripple {
namespace RPC {

static
UseCoroutines defaultUseCoroutines = UseCoroutines::no;

Suspend const dontSuspend = [] (Continuation const& continuation)
{
    continuation([] () {});
};

namespace {

void runOnJobQueue(
    Application& app, std::string const& name, Callback const& callback)
{
    auto cb = [callback] (Job&) { callback(); };
    app.getJobQueue().addJob(jtCLIENT, name, cb);
};

Callback suspendForJobQueue(
    Application& app, Suspend const& suspend, std::string const& jobName)
{
    assert(suspend);
    return Callback( [suspend, jobName, &app] () {
        suspend([jobName, &app] (Callback const& callback) {
            runOnJobQueue(app, jobName, callback);
        });
    });
}

} // namespace

Json::Output chunkedYieldingOutput (
    Json::Output const& output, Callback const& yield, std::size_t chunkSize)
{
    if (!yield)
        return output;

    auto count = std::make_shared <std::size_t> (0);
    return [chunkSize, count, output, yield] (boost::string_ref const& bytes)
    {
        if (*count > chunkSize)
        {
            yield();
            *count = 0;
        }
        output (bytes);
        *count += bytes.size();
    };
}

CountedYield::CountedYield (std::size_t yieldCount, Callback const& yield)
        : yieldCount_ (yieldCount), yield_ (yield)
{
}

void CountedYield::yield()
{
    if (yieldCount_ && yield_)
    {
        if (++count_ >= yieldCount_)
        {
            yield_();
            count_ = 0;
        }
    }
}

UseCoroutines useCoroutines(BasicConfig const& config)
{
    if (auto use = config["section"].get<bool>("use_coroutines"))
        return *use ? UseCoroutines::yes : UseCoroutines::no;
    return defaultUseCoroutines;
}

YieldStrategy makeYieldStrategy (BasicConfig const& config)
{
    auto s = config["section"];
    YieldStrategy ys;
    ys.streaming = get<bool> (s, "streaming") ?
            YieldStrategy::Streaming::yes :
            YieldStrategy::Streaming::no;
    ys.useCoroutines = useCoroutines(config);
    ys.accountYieldCount = get<std::size_t> (s, "account_yield_count");
    ys.transactionYieldCount = get<std::size_t> (s, "transaction_yield_count");

    return ys;
}

JobQueueSuspender::JobQueueSuspender(
    Application& app, Suspend const& susp, std::string const& jobName)
        : suspend(susp ? susp : dontSuspend),
          yield(suspendForJobQueue(app, suspend, jobName))
{
    // There's a non-empty jobName exactly if there's a non-empty Suspend.
    assert(!(susp && jobName.empty()));
}

JobQueueSuspender::JobQueueSuspender(Application &app)
        : JobQueueSuspender(app, {}, {})
{
}

} // RPC
} // ripple
