//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/app/sidechain/impl/SidechainListener.h>

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/sidechain/Federator.h>
#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/json/Output.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {
namespace sidechain {

SidechainListener::SidechainListener(
    Source& source,
    AccountID const& account,
    std::weak_ptr<Federator>&& federator,
    Application& app,
    beast::Journal j)
    : InfoSub(source)
    , ChainListener(
          ChainListener::IsMainchain::no,
          account,
          std::move(federator),
          j)
    , app_(app)
{
}

void
SidechainListener::init(NetworkOPs& netOPs)
{
    auto e = netOPs.subAccountHistory(shared_from_this(), doorAccount_);
    if (e != rpcSUCCESS)
        LogicError("Could not subscribe to side chain door account history.");
}

void
SidechainListener::send(Json::Value const& msg, bool)
{
    processMessage(msg);
}

void
SidechainListener::stopHistoricalTxns(NetworkOPs& netOPs)
{
    netOPs.unsubAccountHistory(
        shared_from_this(), doorAccount_, /*history only*/ true);
}

void
SidechainListener::send(
    std::string const& cmd,
    Json::Value const& params,
    RpcCallback onResponse)
{
    std::weak_ptr<SidechainListener> selfWeak = shared_from_this();
    auto job = [cmd, params, onResponse, selfWeak](Job&) {
        auto self = selfWeak.lock();
        if (!self)
            return;

        JLOGV(
            self->j_.trace(),
            "Sidechain send",
            jv("command", cmd),
            jv("params", params));

        Json::Value const request = [&] {
            Json::Value r(params);
            r[jss::method] = cmd;
            r[jss::jsonrpc] = "2.0";
            r[jss::ripplerpc] = "2.0";
            return r;
        }();
        Resource::Charge loadType = Resource::feeReferenceRPC;
        Resource::Consumer c;
        RPC::JsonContext context{
            {self->j_,
             self->app_,
             loadType,
             self->app_.getOPs(),
             self->app_.getLedgerMaster(),
             c,
             Role::ADMIN,
             {},
             {},
             RPC::apiMaximumSupportedVersion},
            std::move(request)};

        Json::Value jvResult;
        RPC::doCommand(context, jvResult);
        JLOG(self->j_.trace()) << "Sidechain response: " << jvResult;
        if (self->app_.config().standalone())
            self->app_.getOPs().acceptLedger();
        onResponse(jvResult);
    };
    app_.getJobQueue().addJob(jtRPC, "federator rpc", job);
}

}  // namespace sidechain

}  // namespace ripple
