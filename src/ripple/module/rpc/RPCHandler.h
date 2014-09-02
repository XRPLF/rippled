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

#ifndef RIPPLE_APP_RPC_HANDLER
#define RIPPLE_APP_RPC_HANDLER

#include <ripple/module/rpc/impl/AccountFromString.h>
#include <ripple/module/rpc/impl/Accounts.h>
#include <ripple/module/rpc/impl/Authorize.h>
#include <ripple/module/rpc/impl/Context.h>
#include <ripple/module/rpc/impl/GetMasterGenerator.h>
#include <ripple/module/rpc/impl/LookupLedger.h>
#include <ripple/module/rpc/impl/ParseAccountIds.h>
#include <ripple/module/rpc/impl/TransactionSign.h>

namespace ripple {

// used by the RPCServer or WSDoor to carry out these RPC commands
class InfoSub;
class NetworkOPs;

class RPCHandler
{
public:
    explicit RPCHandler (
        NetworkOPs& netOps, InfoSub::pointer infoSub = nullptr);

    Json::Value doCommand (
        Json::Value const& request,
        Config::Role role,
        Resource::Charge& loadType);

    Json::Value doRpcCommand (
        std::string const& command,
        Json::Value const& params,
        Config::Role role,
        Resource::Charge& loadType);

private:
    NetworkOPs& netOps_;
    InfoSub::pointer infoSub_;

    Config::Role role_ = Config::FORBID;
};

} // ripple

#endif
