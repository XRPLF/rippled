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

#ifndef RIPPLED_RIPPLE_RPC_YIELD_H
#define RIPPLED_RIPPLE_RPC_YIELD_H

#include <ripple/rpc/Output.h>
#include <boost/coroutine/all.hpp>

namespace ripple {
namespace RPC {

/** Wrap an Output so it yields after approximately `chunkSize` bytes.
    (It doesn't split up outputs so there might be some extra data.)
 */
template <typename Yield>
Output yieldingOutput (Output output, Yield& yield, std::size_t chunkSize)
{
    auto count = std::make_shared <std::size_t> (0);
    return [output, &yield, count, chunkSize] (Bytes const& bytes)
    {
        output (bytes);
        if ((*count += bytes.size) >= chunkSize)
        {
            yield();
            *count = 0;
        }
    };
}

#ifdef BOOST_156_OR_GREATER
using Coroutine = boost::coroutines::asymmetric_coroutine<void>;
#else
using Coroutine = boost::coroutines::coroutine<void>;
#endif

template <typename OutputFunction>
Coroutine::pull_type yieldingOutputCoroutine(
    OutputFunction function, Output output, std::size_t chunkSize)
{
    auto method = [function, output, chunkSize](Coroutine::push_type& yield)
    {
        function (yieldingOutput (output, yield, chunkSize));
    };

    return Coroutine::pull_type (method);
}

} // RPC
} // ripple

#endif
