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

#ifndef RIPPLE_SIDECHAIN_IMPL_DOOR_OPENER_H
#define RIPPLE_SIDECHAIN_IMPL_DOOR_OPENER_H

#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/app/sidechain/impl/ChainListener.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/AccountID.h>

#include <mutex>
#include <queue>
#include <string>

namespace ripple {
namespace sidechain {

class TicketRunner;
class Federator;

class DoorKeeper
{
public:
    static constexpr std::uint32_t LowWaterMark = 0;
    static constexpr std::uint32_t HighWaterMark = 100;
    static_assert(HighWaterMark > LowWaterMark);
    enum class DoorStatus { open, closing, closed, opening };

private:
    enum class InitializeStatus { waitLedger, waitAccountInfo, initialized };
    struct InitializeData
    {
        InitializeStatus status_ = InitializeStatus::waitLedger;
        std::queue<event::DepositAuthResult> toReplay_;
        std::uint32_t ledgerIndex_ = 0;
    };

    std::shared_ptr<ChainListener> rpcChannel_;
    bool const isMainChain_;
    std::string const accountStr_;
    mutable std::mutex mtx_;
    InitializeData GUARDED_BY(mtx_) initData_;
    DoorStatus GUARDED_BY(mtx_) status_;
    TicketRunner& ticketRunner_;
    Federator& federator_;
    beast::Journal j_;

public:
    DoorKeeper(
        bool isMainChain,
        AccountID const& account,
        TicketRunner& ticketRunner,
        Federator& federator,
        beast::Journal j);
    ~DoorKeeper() = default;

    /**
     * start to initialize the doorKeeper by sending accountInfo RPC
     */
    void
    init() EXCLUDES(mtx_);

    /**
     * process the accountInfo result and set the door status
     * This is the end of initialization
     *
     * @param rpcResult the accountInfo result
     */
    void
    accountInfoResult(Json::Value const& rpcResult) EXCLUDES(mtx_);

    /**
     * update the doorKeeper about the number of pending XChain payments
     * The doorKeeper will close the door if there are too many
     * pending XChain payments and reopen the door later
     *
     * @param length the number of pending XChain payments
     */
    void
    updateQueueLength(std::uint32_t length) EXCLUDES(mtx_);

    /**
     * process a DepositAuthResult event and set the door status.
     * It queues the event if the doorKeeper is not yet initialized.
     *
     * @param e the DepositAuthResult event
     */
    void
    onEvent(event::DepositAuthResult const& e) EXCLUDES(mtx_);

    Json::Value
    getInfo() const EXCLUDES(mtx_);

    void
    setRpcChannel(std::shared_ptr<ChainListener> channel);

private:
    void
    rpcAccountInfo(std::lock_guard<std::mutex> const&) REQUIRES(mtx_);

    void
    processEvent(
        event::DepositAuthResult const& e,
        std::lock_guard<std::mutex> const&) REQUIRES(mtx_);
};

}  // namespace sidechain
}  // namespace ripple

#endif
