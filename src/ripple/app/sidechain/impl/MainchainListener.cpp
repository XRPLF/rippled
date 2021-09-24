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

#include <ripple/app/sidechain/impl/MainchainListener.h>

#include <ripple/app/sidechain/Federator.h>
#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/app/sidechain/impl/WebsocketClient.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/json/Output.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/jss.h>

namespace ripple {
namespace sidechain {

class Federator;

MainchainListener::MainchainListener(
    AccountID const& account,
    std::weak_ptr<Federator>&& federator,
    beast::Journal j)
    : ChainListener(
          ChainListener::IsMainchain::yes,
          account,
          std::move(federator),
          j)
{
}
void
MainchainListener::onMessage(Json::Value const& msg)
{
    auto callbackOpt = [&]() -> std::optional<RpcCallback> {
        if (msg.isMember(jss::id) && msg[jss::id].isIntegral())
        {
            auto callbackId = msg[jss::id].asUInt();
            std::lock_guard lock(callbacksMtx_);
            auto i = callbacks_.find(callbackId);
            if (i != callbacks_.end())
            {
                auto cb = i->second;
                callbacks_.erase(i);
                return cb;
            }
        }
        return {};
    }();

    if (callbackOpt)
    {
        JLOG(j_.trace()) << "Mainchain onMessage, reply to a callback: " << msg;
        assert(msg.isMember(jss::result));
        (*callbackOpt)(msg[jss::result]);
    }
    else
    {
        processMessage(msg);
    }
}

void
MainchainListener::init(
    boost::asio::io_service& ios,
    boost::asio::ip::address const& ip,
    std::uint16_t port)
{
    wsClient_ = std::make_unique<WebsocketClient>(
        [self = shared_from_this()](Json::Value const& msg) {
            self->onMessage(msg);
        },
        ios,
        ip,
        port,
        /*headers*/ std::unordered_map<std::string, std::string>{},
        j_);

    Json::Value params;
    params[jss::account_history_tx_stream] = Json::objectValue;
    params[jss::account_history_tx_stream][jss::account] = doorAccountStr_;
    send("subscribe", params);
}

// destructor must be defined after WebsocketClient size is known (i.e. it can
// not be defaulted in the header or the unique_ptr declration of
// WebsocketClient won't work)
MainchainListener::~MainchainListener() = default;

void
MainchainListener::shutdown()
{
    if (wsClient_)
        wsClient_->shutdown();
}

std::uint32_t
MainchainListener::send(std::string const& cmd, Json::Value const& params)
{
    return wsClient_->send(cmd, params);
}

void
MainchainListener::stopHistoricalTxns()
{
    Json::Value params;
    params[jss::stop_history_tx_only] = true;
    params[jss::account_history_tx_stream] = Json::objectValue;
    params[jss::account_history_tx_stream][jss::account] = doorAccountStr_;
    send("unsubscribe", params);
}

void
MainchainListener::send(
    std::string const& cmd,
    Json::Value const& params,
    RpcCallback onResponse)
{
    JLOGV(
        j_.trace(), "Mainchain send", jv("command", cmd), jv("params", params));

    auto id = wsClient_->send(cmd, params);
    std::lock_guard lock(callbacksMtx_);
    callbacks_.emplace(id, onResponse);
}
}  // namespace sidechain
}  // namespace ripple
