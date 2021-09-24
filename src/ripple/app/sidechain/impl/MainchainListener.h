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

#ifndef RIPPLE_SIDECHAIN_IMPL_MAINCHAINLISTENER_H_INCLUDED
#define RIPPLE_SIDECHAIN_IMPL_MAINCHAINLISTENER_H_INCLUDED

#include <ripple/app/sidechain/impl/ChainListener.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/AccountID.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address.hpp>

#include <memory>

namespace ripple {
namespace sidechain {

class Federator;
class WebsocketClient;

class MainchainListener : public ChainListener,
                          public std::enable_shared_from_this<MainchainListener>
{
    std::unique_ptr<WebsocketClient> wsClient_;

    mutable std::mutex callbacksMtx_;
    std::map<std::uint32_t, RpcCallback> GUARDED_BY(callbacksMtx_) callbacks_;

    void
    onMessage(Json::Value const& msg) EXCLUDES(callbacksMtx_);

public:
    MainchainListener(
        AccountID const& account,
        std::weak_ptr<Federator>&& federator,
        beast::Journal j);

    ~MainchainListener();

    void
    init(
        boost::asio::io_service& ios,
        boost::asio::ip::address const& ip,
        std::uint16_t port);

    // Returns command id that will be returned in the response
    std::uint32_t
    send(std::string const& cmd, Json::Value const& params)
        EXCLUDES(callbacksMtx_);

    void
    shutdown();

    void
    stopHistoricalTxns();

    void
    send(
        std::string const& cmd,
        Json::Value const& params,
        RpcCallback onResponse) override;
};

}  // namespace sidechain
}  // namespace ripple

#endif
