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

using CoroutineType = Continuation;
using CoroutinePull = boost::coroutines::coroutine <CoroutineType>::pull_type;
using CoroutinePush = boost::coroutines::coroutine <CoroutineType>::push_type;

struct Coroutine::Impl : public std::enable_shared_from_this <Coroutine::Impl>
{
    Impl (CoroutinePull&& pull_) : pull (std::move (pull_))
    {
    }

    CoroutinePull pull;

    void run()
    {
        while (pull)
        {
            pull();

            if (! pull)
                return;

            if (auto continuation = pull.get())
            {
                auto that = shared_from_this();
                continuation ([that] () { that->run(); });
                return;
            }
        }
    }
};

Coroutine::Coroutine (SuspendCallback const& suspendCallback)
{
    CoroutinePull pull ([suspendCallback] (CoroutinePush& push)
    {
        Suspend suspend = [&push] (CoroutineType const& cbc) {
            push (cbc);
        };
        suspend ({});
        suspendCallback (suspend);
    });

    impl_ = std::make_shared<Impl> (std::move (pull));
}

Coroutine::~Coroutine() = default;

void Coroutine::run()
{
    assert (impl_);
    if (impl_)
        impl_->run();
    impl_.reset();
}

} // RPC
} // ripple
