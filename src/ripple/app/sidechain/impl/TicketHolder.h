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

#ifndef RIPPLE_SIDECHAIN_IMPL_TICKET_BOOTH_H
#define RIPPLE_SIDECHAIN_IMPL_TICKET_BOOTH_H

#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/app/sidechain/impl/ChainListener.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/AccountID.h>

#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

namespace ripple {
namespace sidechain {

class Federator;

enum class TicketPurpose : std::uint32_t {
    mainDoorKeeper,
    sideDoorKeeper,
    updateSignerList,
    TP_NumberOfItems
};
std::string
TicketPurposeToStr(TicketPurpose tp);

struct AutoRenewedTicket
{
    enum class Status { available, taken };

    std::uint32_t seq_;
    Status status_;

    AutoRenewedTicket() : seq_(0), status_(Status::taken)
    {
    }
};

class TicketHolder
{
    enum class InitializeStatus {
        waitLedger,
        waitAccountObject,
        waitTx,
        needToQueryTx,
        initialized
    };

    struct InitializeData
    {
        InitializeStatus status_ = InitializeStatus::waitLedger;
        hash_map<uint256, std::uint32_t> tickets_;
        std::queue<event::TicketCreateResult> toReplay_;
        std::queue<event::BootstrapTicket> bootstrapTicketToReplay_;
        std::uint32_t ledgerIndex_ = 0;
    };

    std::shared_ptr<ChainListener> rpcChannel_;
    bool isMainChain_;
    std::string const accountStr_;
    AutoRenewedTicket
        tickets_[static_cast<std::underlying_type_t<TicketPurpose>>(
            TicketPurpose::TP_NumberOfItems)];
    InitializeData initData_;
    Federator& federator_;
    beast::Journal j_;
    mutable std::mutex mtx_;

public:
    TicketHolder(
        bool isMainChain,
        AccountID const& account,
        Federator& federator,
        beast::Journal j);

    /**
     * start to initialize the ticketHolder by sending accountObject RPC
     */
    void
    init() EXCLUDES(mtx_);
    /**
     * process accountObject result and find the tickets.
     * Initialization is not completed, because a ticket ledger object
     * does not have information about its purpose.
     * The purpose is in the TicketCreate tx what created the ticket.
     * So the ticketHolder queries the TicketCreate tx for each ticket found.
     * @param rpcResult accountObject result
     */
    void
    accountObjectResult(Json::Value const& rpcResult) EXCLUDES(mtx_);
    /**
     * process tx RPC result
     * Initialization is completed once all TicketCreate txns are found,
     * one for every ticket found in the previous initialization step.
     * @param rpcResult tx result
     */
    void
    txResult(Json::Value const& rpcResult) EXCLUDES(mtx_);

    enum class PeekOrTake { peek, take };
    /**
     * take or peek the ticket for a purpose
     * @param purpose the ticket purpose
     * @param pt take or peek
     * @return the ticket if exist and not taken
     */
    std::optional<std::uint32_t>
    getTicket(TicketPurpose purpose, PeekOrTake pt) EXCLUDES(mtx_);

    /**
     * process a TicketCreateResult event, update the ticket number and status
     * It queues the event if the ticketHolder is not yet initialized.
     *
     * @param e the TicketCreateResult event
     */
    void
    onEvent(event::TicketCreateResult const& e) EXCLUDES(mtx_);
    /**
     * process a ticket created during network bootstrap
     * @param e the BootstrapTicket event
     */
    void
    onEvent(event::BootstrapTicket const& e) EXCLUDES(mtx_);

    Json::Value
    getInfo() const EXCLUDES(mtx_);

    void
    setRpcChannel(std::shared_ptr<ChainListener> channel);

private:
    void
    rpcAccountObject();

    void
    rpcTx(std::lock_guard<std::mutex> const&) REQUIRES(mtx_);

    // replay accumulated events before finish initialization
    void
    replay(std::lock_guard<std::mutex> const&) REQUIRES(mtx_);
    template <class E>
    void
    processEvent(E const& e, std::lock_guard<std::mutex> const&) REQUIRES(mtx_);
};

class TicketRunner
{
    std::string const mainAccountStr_;
    std::string const sideAccountStr_;
    Federator& federator_;
    TicketHolder mainHolder_;
    TicketHolder sideHolder_;
    beast::Journal j_;
    // Only one thread at a time can grab tickets
    mutable std::mutex mtx_;

public:
    TicketRunner(
        AccountID const& mainAccount,
        AccountID const& sideAccount,
        Federator& federator,
        beast::Journal j);

    // set RpcChannel for a ticketHolder
    void
    setRpcChannel(bool isMainChain, std::shared_ptr<ChainListener> channel);
    // init a ticketHolder
    void
    init(bool isMainChain);
    // pass a accountObject RPC result to a ticketHolder
    void
    accountObjectResult(bool isMainChain, Json::Value const& rpcResult);
    // pass a tx RPC result to a ticketHolder
    void
    txResult(bool isMainChain, Json::Value const& rpcResult);

    /**
     * Start to run a protocol that submit a federator account control tx
     * to the network.
     *
     * Comparing to a normal tx submission that takes one step, a federator
     * account control tx (such as depositAuth and signerListSet) takes 3 steps:
     * 1. use a ticket to send a accountSet no-op tx as a trigger
     * 2. create a new ticket
     * 3. submit the account control tx
     *
     * @param ticketPurpose the purpose of ticket. The purpose describes
     * the account control tx use case.
     * @param mainTxJson account control tx for main chain
     * @param sideTxJson account control tx for side chain
     * @note mainTxJson and sideTxJson cannot both be empty
     * @return if the protocol started
     */
    [[nodiscard]] bool
    trigger(
        TicketPurpose ticketPurpose,
        std::optional<Json::Value> const& mainTxJson,
        std::optional<Json::Value> const& sideTxJson) EXCLUDES(mtx_);

    /**
     * process a TicketCreateTrigger event, by submitting TicketCreate tx
     *
     * This event is generated when the accountSet no-op tx
     * (as the protocol trigger) appears in the tx stream,
     * i.e. sorted with regular XChain payments.
     */
    void
    onEvent(std::uint32_t accountSeq, event::TicketCreateTrigger const& e);
    /**
     * process a TicketCreateResult event, update the ticketHolder.
     *
     * This event is generated when the TicketCreate tx appears
     * in the tx stream.
     */
    void
    onEvent(std::uint32_t accountSeq, event::TicketCreateResult const& e);

    /**
     * process a ticket created during network bootstrap
     */
    void
    onEvent(event::BootstrapTicket const& e);

    Json::Value
    getInfo(bool isMainchain) const;
};

}  // namespace sidechain
}  // namespace ripple

#endif
