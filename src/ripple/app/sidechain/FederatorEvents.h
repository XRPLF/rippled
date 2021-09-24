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

#ifndef RIPPLE_SIDECHAIN_FEDERATOR_EVENTS_H_INCLUDED
#define RIPPLE_SIDECHAIN_FEDERATOR_EVENTS_H_INCLUDED

#include <ripple/beast/utility/Journal.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/TER.h>
#include <beast/utility/Journal.h>

#include <boost/format.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <variant>

namespace ripple {
namespace sidechain {
namespace event {

enum class Dir { sideToMain, mainToSide };
enum class AccountFlagOp { set, clear };
static constexpr std::uint32_t MemoStringMax = 512;

enum class EventType {
    bootstrap,
    trigger,
    result,
    resultAndTrigger,
    heartbeat,
    startOfTransactions
};

// A cross chain transfer was detected on this federator
struct XChainTransferDetected
{
    // direction of the transfer
    Dir dir_;
    // Src account on the src chain
    AccountID src_;
    // Dst account on the dst chain
    AccountID dst_;
    STAmount deliveredAmt_;
    std::uint32_t txnSeq_;
    uint256 txnHash_;
    std::int32_t rpcOrder_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct HeartbeatTimer
{
    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct XChainTransferResult
{
    // direction is the direction of the triggering transaction.
    // I.e. A "mainToSide" transfer result is a transaction that
    // happens on the sidechain (the triggering transaction happended on the
    // mainchain)
    Dir dir_;
    AccountID dst_;
    std::optional<STAmount> deliveredAmt_;
    std::uint32_t txnSeq_;
    // Txn hash of the initiating xchain transaction
    uint256 srcChainTxnHash_;
    // Txn has of the federator's transaction on the dst chain
    uint256 txnHash_;
    TER ter_;
    std::int32_t rpcOrder_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct RefundTransferResult
{
    // direction is the direction of the triggering transaction.
    // I.e. A "mainToSide" refund transfer result is a transaction that
    // happens on the mainchain (the triggering transaction happended on the
    // mainchain, the failed result happened on the side chain, and the refund
    // result happened on the mainchain)
    Dir dir_;
    AccountID dst_;
    std::optional<STAmount> deliveredAmt_;
    std::uint32_t txnSeq_;
    // Txn hash of the initiating xchain transaction
    uint256 srcChainTxnHash_;
    // Txn hash of the federator's transaction on the dst chain
    uint256 dstChainTxnHash_;
    // Txn hash of the refund result
    uint256 txnHash_;
    TER ter_;
    std::int32_t rpcOrder_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

// The start of historic transactions has been reached
struct StartOfHistoricTransactions
{
    bool isMainchain_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct TicketCreateTrigger
{
    Dir dir_;
    bool success_;
    std::uint32_t txnSeq_;
    std::uint32_t ledgerIndex_;
    uint256 txnHash_;
    std::int32_t rpcOrder_;

    std::uint32_t sourceTag_;
    std::string memoStr_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct TicketCreateResult
{
    Dir dir_;
    bool success_;
    std::uint32_t txnSeq_;
    std::uint32_t ledgerIndex_;
    uint256 srcChainTxnHash_;
    uint256 txnHash_;
    std::int32_t rpcOrder_;

    std::uint32_t sourceTag_;
    std::string memoStr_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;

    void
    removeTrigger();
};

struct DepositAuthResult
{
    Dir dir_;
    bool success_;
    std::uint32_t txnSeq_;
    std::uint32_t ledgerIndex_;
    uint256 srcChainTxnHash_;
    std::int32_t rpcOrder_;

    AccountFlagOp op_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct SignerListSetResult
{
    // TODO

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct BootstrapTicket
{
    bool isMainchain_;
    bool success_;
    std::uint32_t txnSeq_;
    std::uint32_t ledgerIndex_;
    std::int32_t rpcOrder_;

    std::uint32_t sourceTag_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

struct DisableMasterKeyResult
{
    bool isMainchain_;
    std::uint32_t txnSeq_;
    std::int32_t rpcOrder_;

    EventType
    eventType() const;

    Json::Value
    toJson() const;
};

}  // namespace event

using FederatorEvent = std::variant<
    event::XChainTransferDetected,
    event::HeartbeatTimer,
    event::XChainTransferResult,
    event::RefundTransferResult,
    event::StartOfHistoricTransactions,
    event::TicketCreateTrigger,
    event::TicketCreateResult,
    event::DepositAuthResult,
    event::BootstrapTicket,
    event::DisableMasterKeyResult>;

event::EventType
eventType(FederatorEvent const& event);

Json::Value
toJson(FederatorEvent const& event);

// If the event has a txnHash_ field (all the trigger events), return the hash,
// otherwise return nullopt
std::optional<uint256>
txnHash(FederatorEvent const& event);

}  // namespace sidechain
}  // namespace ripple

#endif
