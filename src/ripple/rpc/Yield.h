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
#include <functional>

namespace ripple {
namespace RPC {

/** Yield is a generic placeholder for a function that yields control of
    execution - perhaps to another coroutine.

    When code calls Yield, it might block for an indeterminate period of time.

    By convention you must not be holding any locks or any resource that would
    prevent any other task from making forward progress when you call Yield.
*/
using Yield = std::function <void()>;

/** Coroutine is the controller class for RPC coroutines. */
using Coroutine = boost::coroutines::coroutine <void>::pull_type;

/** Wrap an Output so it yields after approximately `chunkSize` bytes.

    chunkedYieldingOutput() only yields after a call to output(), so there might
    more than chunkSize bytes sent between calls to yield().
 */
Output chunkedYieldingOutput (
    Output const&, Yield const&, std::size_t chunkSize);

/** Run a function that's expecting a Yield as a coroutine.

    This adaptor from Coroutine to Yield is necessary:
      * because Coroutine immediately starts operating in the constructor
        but we want to defer operation to the loop.
      * because Coroutine is not copyable.
      * because Coroutine doesn't have an empty constructor.
      * because C++11 lambdas don't handle move parameters yet.
 */
template <typename OutputFunction>
Coroutine yieldingCoroutine (OutputFunction function)
{
    return Coroutine([=] (boost::coroutines::coroutine <void>::push_type& push)
    {
        Yield yield = [&] () { push(); };
        yield ();
        function (yield);
    });
}

} // RPC
} // ripple

#endif
