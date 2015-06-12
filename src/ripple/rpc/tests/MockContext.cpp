//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/rpc/tests/MockContext.h>
#include <ripple/rpc/tests/MockNetworkOPs.h>
#include <ripple/rpc/Context.h>

namespace ripple {
namespace RPC {

struct MockContext::Impl
{
    beast::RootStoppable parent;
    MockNetworkOPs netOps;
    Resource::Charge loadType = Resource::Charge (0);
    Context context;

    Impl() : parent ("MockContext"),
             netOps (parent),
             context {Json::objectValue, loadType, netOps, Role::USER}
    {
        context.yield = [] () {};
    }
};

MockContext::MockContext() : impl_ (std::make_unique<Impl>())
{}

MockContext::~MockContext() = default;

Context& MockContext::context()
{
    return impl_->context;
}

} // RPC
} // ripple
