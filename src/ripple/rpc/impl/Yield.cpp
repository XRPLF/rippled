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
#include <ripple/rpc/Yield.h>
#include <ripple/rpc/tests/TestOutputSuite.test.h>

namespace ripple {
namespace RPC {

Json::Output chunkedYieldingOutput (
    Json::Output const& output, Yield const& yield, std::size_t chunkSize)
{
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


CountedYield::CountedYield (std::size_t yieldCount, Yield const& yield)
        : yieldCount_ (yieldCount), yield_ (yield)
{
}

void CountedYield::yield()
{
    if (yieldCount_) {
        if (++count_ >= yieldCount_)
        {
            yield_();
            count_ = 0;
        }
    }
}

YieldStrategy makeYieldStrategy (Section const& s)
{
    YieldStrategy ys;
    ys.streaming = get<bool> (s, "streaming") ?
            YieldStrategy::Streaming::yes :
            YieldStrategy::Streaming::no;
    ys.useCoroutines = get<bool> (s, "use_coroutines") ?
            YieldStrategy::UseCoroutines::yes :
            YieldStrategy::UseCoroutines::no;
    ys.byteYieldCount = get<std::size_t> (s, "byte_yield_count");
    ys.accountYieldCount = get<std::size_t> (s, "account_yield_count");
    ys.transactionYieldCount = get<std::size_t> (s, "transaction_yield_count");

    return ys;
}

} // RPC
} // ripple
