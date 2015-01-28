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

#ifndef RIPPLE_RPC_RPCHANDLER_H_INCLUDED
#define RIPPLE_RPC_RPCHANDLER_H_INCLUDED

#include <ripple/core/Config.h>
#include <ripple/net/InfoSub.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Status.h>

namespace ripple {
namespace RPC {

struct Context;
struct YieldStrategy;

/** Execute an RPC command and store the results in a Json::Value. */
Status doCommand (RPC::Context&, Json::Value&, YieldStrategy const& s = {});

/** Execute an RPC command and store the results in an std::string. */
void executeRPC (RPC::Context&, std::string&, YieldStrategy const& s = {});

/** Temporary flag to enable RPCs. */
auto const streamingRPC = false;

} // RPC
} // ripple

#endif
