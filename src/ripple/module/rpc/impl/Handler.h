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

#ifndef RIPPLE_RPC_HANDLER
#define RIPPLE_RPC_HANDLER

#include <ripple/module/rpc/RPCHandler.h>

namespace ripple {
namespace RPC {

// Under what condition can we call this RPC?
enum Condition {
    NO_CONDITION     = 0,
    NEEDS_NETWORK_CONNECTION  = 1,
    NEEDS_CURRENT_LEDGER  = 2 + NEEDS_NETWORK_CONNECTION,
    NEEDS_CLOSED_LEDGER   = 4 + NEEDS_NETWORK_CONNECTION,
};

struct Handler
{
    typedef Json::Value (*Method) (Context&);

    const char* name_;
    Method method_;
    Config::Role role_;
    RPC::Condition condition_;
};

const Handler* getHandler(std::string name);

} // RPC
} // ripple

#endif
