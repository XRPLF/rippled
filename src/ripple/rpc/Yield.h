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

#ifndef RIPPLE_RPC_YIELD_H_INCLUDED
#define RIPPLE_RPC_YIELD_H_INCLUDED

#include <ripple/core/JobQueue.h>
#include <ripple/json/Output.h>
#include <beast/win32_workaround.h>
#include <boost/coroutine/all.hpp>
#include <functional>

namespace ripple {

class Application;
class BasicConfig;
class JobQueue;
class Section;

namespace RPC {

/** See the README.md in this directory for more information about how
    the RPC yield mechanism works.
 */

/** Callback: do something and eventually return. Can't be empty. */
using Callback = std::function <void ()>;

/** Continuation: do something, guarantee to eventually call Callback.
    Can't be empty. */
using Continuation = std::function <void (Callback const&)>;

/** Suspend: suspend execution, pending completion of a Continuation.
    Can't be empty. */
using Suspend = std::function <void (Continuation const&)>;

/** A non-empty Suspend that immediately calls its callback. */
extern
Suspend const dontSuspend;

/** Wrap an Output so it yields after approximately `chunkSize` bytes.

    chunkedYieldingOutput() only yields after a call to output(), so there might
    more than chunkSize bytes sent between calls to yield().

    chunkedYieldingOutput() also only yields before it's about to output more
    data.  This is to avoid the case where you yield after outputting data, but
    then never send more data.
 */
Json::Output chunkedYieldingOutput (
    Json::Output const&, Callback const&, std::size_t chunkSize);

/** Yield every yieldCount calls.  If yieldCount is 0, never yield. */
class CountedYield
{
public:
    CountedYield (std::size_t yieldCount, Callback const& yield);
    void yield();

private:
    std::size_t count_ = 0;
    std::size_t const yieldCount_;
    Callback const yield_;
};

enum class UseCoroutines {no, yes};

/** When do we yield when performing a ledger computation? */
struct YieldStrategy
{
    enum class Streaming {no, yes};

    /** Is the data streamed, or generated monolithically? */
    Streaming streaming = Streaming::no;

    /** Are results generated in a coroutine?  If this is no, then the code can
        never yield. */
    UseCoroutines useCoroutines = UseCoroutines::no;

    /** How many accounts do we process before yielding?  0 means "never yield
        due to number of accounts processed." */
    std::size_t accountYieldCount = 0;

    /** How many transactions do we process before yielding? 0 means "never
        yield due to number of transactions processed." */
    std::size_t transactionYieldCount = 0;
};

/** Does a BasicConfig require the use of coroutines? */
UseCoroutines useCoroutines(BasicConfig const&);

/** Create a yield strategy from a BasicConfig. */
YieldStrategy makeYieldStrategy(BasicConfig const&);

/** JobQueueSuspender is a suspend, with a yield that reschedules the job
    on the job queue. */
struct JobQueueSuspender
{
    /** Possibly suspend current execution. */
    Suspend const suspend;

    /** Possibly yield and restart on the job queue. */
    Callback const yield;

    /** Create a JobQueueSuspender where yield does nothing and the suspend
        immediately executes the continuation. */
    JobQueueSuspender(Application&);

    /** Create a JobQueueSuspender with a Suspend.

        When yield is called, it reschedules the current job on the JobQueue
        with the given jobName. */
    JobQueueSuspender(Application&, Suspend const&, std::string const& jobName);
};

} // RPC
} // ripple

#endif
