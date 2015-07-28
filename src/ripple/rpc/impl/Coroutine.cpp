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
#include <ripple/rpc/Coroutine.h>
#include <ripple/rpc/tests/TestOutputSuite.test.h>
#include <iostream>

namespace ripple {
namespace RPC {
namespace {

using CoroutineType = Continuation;
using BoostCoroutine = boost::coroutines::asymmetric_coroutine<CoroutineType>;
using Pull = BoostCoroutine::pull_type;
using Push = BoostCoroutine::push_type;

void runOnCoroutineImpl(std::shared_ptr<Pull> pull)
{
    while (*pull)
    {
        (*pull)();

        if (! *pull)
            return;

        if (auto continuation = pull->get())
        {
            continuation ([pull] () { runOnCoroutineImpl(pull); });
            return;
        }
    }
}

} // namespace

void runOnCoroutine(Coroutine const& coroutine)
{
    auto pullFunction = [coroutine] (Push& push) {
        Suspend suspend = [&push] (CoroutineType const& cbc) {
            if (push)
                push (cbc);
        };

        // Run once doing nothing, to get the other side started.
        suspend([] (Callback const& callback) { callback(); });

        // Now run the coroutine.
        coroutine(suspend);
    };

    runOnCoroutineImpl(std::make_shared<Pull>(pullFunction));
}

void runOnCoroutine(UseCoroutines useCoroutines, Coroutine const& coroutine)
{
    if (useCoroutines == UseCoroutines::yes)
        runOnCoroutine(coroutine);
    else
        coroutine(dontSuspend);
}

} // RPC
} // ripple
