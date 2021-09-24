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

#ifndef RIPPLE_SIDECHAIN_IMPL_CHAINLISTENER_H_INCLUDED
#define RIPPLE_SIDECHAIN_IMPL_CHAINLISTENER_H_INCLUDED

#include <ripple/protocol/AccountID.h>

#include <ripple/app/sidechain/impl/InitialSync.h>
#include <ripple/beast/utility/Journal.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address.hpp>

#include <memory>
#include <mutex>
#include <string>

namespace ripple {
namespace sidechain {

class Federator;
class WebsocketClient;

class ChainListener
{
protected:
    enum class IsMainchain { no, yes };

    bool const isMainchain_;
    // Sending xrp to the door account will trigger a x-chain transaction
    AccountID const doorAccount_;
    std::string const doorAccountStr_;
    std::weak_ptr<Federator> federator_;
    mutable std::mutex m_;
    // Logic to handle potentially collecting and replaying historical
    // transactions. Will be empty after replaying.
    std::unique_ptr<InitialSync> GUARDED_BY(m_) initialSync_;
    beast::Journal j_;

    ChainListener(
        IsMainchain isMainchain,
        AccountID const& account,
        std::weak_ptr<Federator>&& federator,
        beast::Journal j);

    virtual ~ChainListener();

    std::string const&
    chainName() const;

    void
    processMessage(Json::Value const& msg) EXCLUDES(m_);

    template <class E>
    void
    pushEvent(E&& e, int txHistoryIndex, std::lock_guard<std::mutex> const&)
        REQUIRES(m_);

public:
    void
    setLastXChainTxnWithResult(uint256 const& hash) EXCLUDES(m_);
    void
    setNoLastXChainTxnWithResult() EXCLUDES(m_);

    Json::Value
    getInfo() const EXCLUDES(m_);

    using RpcCallback = std::function<void(Json::Value const&)>;

    /**
     * send a RPC and call the callback with the RPC result
     * @param cmd PRC command
     * @param params RPC command parameter
     * @param onResponse callback to process RPC result
     */
    virtual void
    send(
        std::string const& cmd,
        Json::Value const& params,
        RpcCallback onResponse) = 0;
};

}  // namespace sidechain
}  // namespace ripple

#endif
