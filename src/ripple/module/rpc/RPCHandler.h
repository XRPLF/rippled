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
class NetworkOPs;
class InfoSub;

class RPCHandler
{
public:
    explicit RPCHandler (NetworkOPs& netOps);

    RPCHandler (NetworkOPs& netOps, InfoSub::pointer infoSub);

    Json::Value doCommand (
        const Json::Value& jvRequest, Config::Role role,
        Resource::Charge& loadType);

    Json::Value doRpcCommand (
        const std::string& strCommand, Json::Value const& jvParams,
        Config::Role iRole, Resource::Charge& loadType);

    // Utilities

private:
    NetworkOPs*         mNetOps;
    InfoSub::pointer    mInfoSub;

    Config::Role mRole;
};

class RPCInternalHandler
{
public:
    typedef Json::Value (*handler_t) (const Json::Value&);

public:
    RPCInternalHandler (const std::string& name, handler_t handler);
    static Json::Value runHandler (const std::string& name, const Json::Value& params);

private:
    // VFALCO TODO Replace with a singleton with a well defined interface and
    //             a lock free stack (if necessary).
    //
    static RPCInternalHandler*  sHeadHandler;

    RPCInternalHandler*         mNextHandler;
    std::string                 mName;
    handler_t                   mHandler;
};

} // ripple

#endif
