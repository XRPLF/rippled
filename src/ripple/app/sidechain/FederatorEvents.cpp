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

#include <ripple/app/sidechain/FederatorEvents.h>

#include <string_view>
#include <type_traits>

namespace ripple {
namespace sidechain {
namespace event {

namespace {

std::string const&
to_string(Dir dir)
{
    switch (dir)
    {
        case Dir::mainToSide: {
            static std::string const r("main");
            return r;
        }
        case Dir::sideToMain: {
            static std::string const r("side");
            return r;
        }
    }

    // Some compilers will warn about not returning from all control paths
    // without this, but this code will never execute.
    assert(0);
    static std::string const error("error");
    return error;
}

std::string const&
to_string(AccountFlagOp op)
{
    switch (op)
    {
        case AccountFlagOp::set: {
            static std::string const r("set");
            return r;
        }
        case AccountFlagOp::clear: {
            static std::string const r("clear");
            return r;
        }
    }

    // Some compilers will warn about not returning from all control paths
    // without this, but this code will never execute.
    assert(0);
    static std::string const error("error");
    return error;
}

}  // namespace

EventType
XChainTransferDetected::eventType() const
{
    return EventType::trigger;
}

Json::Value
XChainTransferDetected::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "XChainTransferDetected";
    result["src"] = toBase58(src_);
    result["dst"] = toBase58(dst_);
    result["deliveredAmt"] = deliveredAmt_.getJson(JsonOptions::none);
    result["txnSeq"] = txnSeq_;
    result["txnHash"] = to_string(txnHash_);
    result["rpcOrder"] = rpcOrder_;
    return result;
}

EventType
HeartbeatTimer::eventType() const
{
    return EventType::heartbeat;
}

Json::Value
HeartbeatTimer::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "HeartbeatTimer";
    return result;
}

EventType
XChainTransferResult::eventType() const
{
    return EventType::result;
}

Json::Value
XChainTransferResult::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "XChainTransferResult";
    result["dir"] = to_string(dir_);
    result["dst"] = toBase58(dst_);
    if (deliveredAmt_)
        result["deliveredAmt"] = deliveredAmt_->getJson(JsonOptions::none);
    result["txnSeq"] = txnSeq_;
    result["srcChainTxnHash"] = to_string(srcChainTxnHash_);
    result["txnHash"] = to_string(txnHash_);
    result["ter"] = transHuman(ter_);
    result["rpcOrder"] = rpcOrder_;
    return result;
}

EventType
RefundTransferResult::eventType() const
{
    return EventType::result;
}

Json::Value
RefundTransferResult::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "RefundTransferResult";
    result["dir"] = to_string(dir_);
    result["dst"] = toBase58(dst_);
    if (deliveredAmt_)
        result["deliveredAmt"] = deliveredAmt_->getJson(JsonOptions::none);
    result["txnSeq"] = txnSeq_;
    result["srcChainTxnHash"] = to_string(srcChainTxnHash_);
    result["dstChainTxnHash"] = to_string(dstChainTxnHash_);
    result["txnHash"] = to_string(txnHash_);
    result["ter"] = transHuman(ter_);
    result["rpcOrder"] = rpcOrder_;
    return result;
}

EventType
TicketCreateTrigger::eventType() const
{
    return EventType::trigger;
}

Json::Value
TicketCreateTrigger::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "TicketCreateTrigger";
    result["dir"] = to_string(dir_);
    result["success"] = success_;
    result["txnSeq"] = txnSeq_;
    result["ledgerIndex"] = ledgerIndex_;
    result["txnHash"] = to_string(txnHash_);
    result["rpcOrder"] = rpcOrder_;
    result["sourceTag"] = sourceTag_;
    result["memo"] = memoStr_;
    return result;
}

EventType
TicketCreateResult::eventType() const
{
    return EventType::resultAndTrigger;
}

Json::Value
TicketCreateResult::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "TicketCreateResult";
    result["dir"] = to_string(dir_);
    result["success"] = success_;
    result["txnSeq"] = txnSeq_;
    result["ledgerIndex"] = ledgerIndex_;
    result["srcChainTxnHash"] = to_string(srcChainTxnHash_);
    result["txnHash"] = to_string(txnHash_);
    result["rpcOrder"] = rpcOrder_;
    result["sourceTag"] = sourceTag_;
    result["memo"] = memoStr_;
    return result;
}

void
TicketCreateResult::removeTrigger()
{
    memoStr_.clear();
}

EventType
DepositAuthResult::eventType() const
{
    return EventType::result;
}

Json::Value
DepositAuthResult::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "DepositAuthResult";
    result["dir"] = to_string(dir_);
    result["success"] = success_;
    result["txnSeq"] = txnSeq_;
    result["ledgerIndex"] = ledgerIndex_;
    result["srcChainTxnHash"] = to_string(srcChainTxnHash_);
    result["rpcOrder"] = rpcOrder_;
    result["op"] = to_string(op_);
    return result;
}

EventType
SignerListSetResult::eventType() const
{
    return EventType::result;
}

Json::Value
SignerListSetResult::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "SignerListSetResult";
    return result;
}
EventType
BootstrapTicket::eventType() const
{
    return EventType::bootstrap;
}

Json::Value
BootstrapTicket::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "BootstrapTicket";
    result["isMainchain"] = isMainchain_;
    result["txnSeq"] = txnSeq_;
    result["rpcOrder"] = rpcOrder_;
    return result;
}
EventType
DisableMasterKeyResult::eventType() const
{
    return EventType::result;  // TODO change to bootstrap type too?
}

Json::Value
DisableMasterKeyResult::toJson() const
{
    Json::Value result{Json::objectValue};
    result["eventType"] = "DisableMasterKeyResult";
    result["isMainchain"] = isMainchain_;
    result["txnSeq"] = txnSeq_;
    result["rpcOrder"] = rpcOrder_;
    return result;
}

}  // namespace event

namespace {
template <typename T, typename = void>
struct hasTxnHash : std::false_type
{
};
template <typename T>
struct hasTxnHash<T, std::void_t<decltype(std::declval<T>().txnHash_)>>
    : std::true_type
{
};
template <class T>
inline constexpr bool hasTxnHash_v = hasTxnHash<T>::value;

// Check that the traits work as expected
static_assert(
    hasTxnHash_v<event::XChainTransferResult> &&
        !hasTxnHash_v<event::HeartbeatTimer>,
    "");
}  // namespace

std::optional<uint256>
txnHash(FederatorEvent const& event)
{
    return std::visit(
        [](auto const& e) -> std::optional<uint256> {
            if constexpr (hasTxnHash_v<std::decay_t<decltype(e)>>)
            {
                return e.txnHash_;
            }
            return std::nullopt;
        },
        event);
}

event::EventType
eventType(FederatorEvent const& event)
{
    return std::visit([](auto const& e) { return e.eventType(); }, event);
}

Json::Value
toJson(FederatorEvent const& event)
{
    return std::visit([](auto const& e) { return e.toJson(); }, event);
}

}  // namespace sidechain
}  // namespace ripple
