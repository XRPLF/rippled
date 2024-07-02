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

#include <xrpld/core/Config.h>
#include <xrpld/net/InfoSub.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>

namespace ripple {
namespace RPC {

struct JsonContext;

/** Execute an RPC command and store the results in a Json::Value. */
Status
doCommand(RPC::JsonContext&, Json::Value&);

Role
roleRequired(unsigned int version, bool betaEnabled, std::string const& method);

}  // namespace RPC
}  // namespace ripple

#endif
