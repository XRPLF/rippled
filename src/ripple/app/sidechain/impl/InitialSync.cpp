
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

#include <ripple/app/sidechain/impl/InitialSync.h>

#include <ripple/app/sidechain/Federator.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/json/json_writer.h>

#include <type_traits>

namespace ripple {
namespace sidechain {

InitialSync::InitialSync(
    std::weak_ptr<Federator> federator,
    bool isMainchain,
    beast::Journal j)
    : federator_{std::move(federator)}, isMainchain_{isMainchain}, j_{j}
{
}

bool
InitialSync::hasTransaction(
    uint256 const& txnHash,
    std::lock_guard<std::mutex> const&) const
{
    return seenTriggeringTxns_.count(txnHash);
}

bool
InitialSync::canReplay(std::lock_guard<std::mutex> const&) const
{
    return !(
        needsLastXChainTxn_ || needsOtherChainLastXChainTxn_ ||
        needsReplayStartTxnHash_);
}

void
InitialSync::stopHistoricalTxns(std::lock_guard<std::mutex> const&)
{
    if (!acquiringHistoricData_)
        return;

    acquiringHistoricData_ = false;
    if (auto f = federator_.lock())
    {
        f->stopHistoricalTxns(getChainType(isMainchain_));
    }
}

void
InitialSync::done()
{
    if (auto f = federator_.lock())
    {
        f->initialSyncDone(
            isMainchain_ ? Federator::ChainType::mainChain
                         : Federator::ChainType::sideChain);
    }
}

bool
InitialSync::setLastXChainTxnWithResult(uint256 const& hash)
{
    std::lock_guard l{m_};
    JLOGV(
        j_.trace(),
        "last xchain txn with result",
        jv("needsOtherChainLastXChainTxn", needsOtherChainLastXChainTxn_),
        jv("isMainchain", isMainchain_),
        jv("hash", hash));
    assert(lastXChainTxnWithResult_.value_or(hash) == hash);
    if (hasReplayed_ || lastXChainTxnWithResult_)
        return hasReplayed_;

    lastXChainTxnWithResult_ = hash;
    needsReplayStartTxnHash_ = false;
    if (needsLastXChainTxn_)
    {
        needsLastXChainTxn_ =
            !seenTriggeringTxns_.count(*lastXChainTxnWithResult_);
    }

    if (!acquiringHistoricData_ && needsLastXChainTxn_)
        LogicError("Initial sync could not find historic XChain transaction");

    if (canReplay(l))
        replay(l);

    return hasReplayed_;
}

bool
InitialSync::setNoLastXChainTxnWithResult()
{
    std::lock_guard l{m_};
    JLOGV(
        j_.trace(),
        "no last xchain txn with result",
        jv("needsOtherChainLastXChainTxn", needsOtherChainLastXChainTxn_),
        jv("isMainchain", isMainchain_));
    assert(!lastXChainTxnWithResult_);
    if (hasReplayed_)
        return hasReplayed_;

    needsLastXChainTxn_ = false;
    needsReplayStartTxnHash_ = false;

    if (canReplay(l))
        replay(l);

    return hasReplayed_;
}

void
InitialSync::replay(std::lock_guard<std::mutex> const& l)
{
    if (hasReplayed_)
        return;

    assert(canReplay(l));

    // Note that this function may push a large number of events to the
    // federator, and it runs under a lock. However, pushing an event to the
    // federator just copies it into a collection (it does not handle the event
    // in the same thread). So this should run relatively quickly.
    stopHistoricalTxns(l);
    hasReplayed_ = true;
    JLOGV(
        j_.trace(),
        "InitialSync replay,",
        jv("chain_name", (isMainchain_ ? "Mainchain" : "Sidechain")),
        jv("lastXChainTxnWithResult_",
           (lastXChainTxnWithResult_ ? strHex(*lastXChainTxnWithResult_)
                                     : "not set")));

    if (lastXChainTxnWithResult_)
        assert(seenTriggeringTxns_.count(*lastXChainTxnWithResult_));

    if (lastXChainTxnWithResult_ &&
        seenTriggeringTxns_.count(*lastXChainTxnWithResult_))
    {
        // Remove the XChainTransferDetected event associated with this txn, and
        // all the XChainTransferDetected events before it. They have already
        // been submitted. If they are not removed, they will never collect
        // enough signatures to be submitted (since the other federators have
        // already submitted it), and it will prevent subsequent event from
        // replaying.
        std::vector<decltype(pendingEvents_)::const_iterator> toRemove;
        toRemove.reserve(pendingEvents_.size());
        std::vector<decltype(pendingEvents_)::const_iterator> toRemoveTrigger;
        bool matched = false;
        for (auto i = pendingEvents_.cbegin(), e = pendingEvents_.cend();
             i != e;
             ++i)
        {
            auto const et = eventType(i->second);
            if (et == event::EventType::trigger)
            {
                toRemove.push_back(i);
            }
            else if (et == event::EventType::resultAndTrigger)
            {
                toRemoveTrigger.push_back(i);
            }
            else
            {
                continue;
            }

            auto const txnHash = sidechain::txnHash(i->second);
            if (!txnHash)
            {
                // All triggering events should have a txnHash
                assert(0);
                continue;
            }
            JLOGV(
                j_.trace(),
                "InitialSync replay, remove trigger event from pendingEvents_",
                jv("chain_name", (isMainchain_ ? "Mainchain" : "Sidechain")),
                jv("txnHash", *txnHash));
            if (*lastXChainTxnWithResult_ == *txnHash)
            {
                matched = true;
                break;
            }
        }
        assert(matched);
        if (matched)
        {
            for (auto i : toRemoveTrigger)
            {
                if (auto ticketResult = std::get_if<event::TicketCreateResult>(
                        &(pendingEvents_.erase(i, i)->second));
                    ticketResult)
                {
                    ticketResult->removeTrigger();
                }
            }
            for (auto i = toRemove.begin(), e = toRemove.end(); i != e; ++i)
            {
                pendingEvents_.erase(*i);
            }
        }
    }

    if (auto f = federator_.lock())
    {
        for (auto&& [_, e] : pendingEvents_)
            f->push(std::move(e));
    }

    seenTriggeringTxns_.clear();
    pendingEvents_.clear();
    done();
}

bool
InitialSync::onEvent(event::XChainTransferDetected&& e)
{
    return onTriggerEvent(std::move(e));
}

bool
InitialSync::onEvent(event::XChainTransferResult&& e)
{
    return onResultEvent(std::move(e), 1);
}

bool
InitialSync::onEvent(event::TicketCreateTrigger&& e)
{
    return onTriggerEvent(std::move(e));
}

bool
InitialSync::onEvent(event::TicketCreateResult&& e)
{
    static_assert(std::is_rvalue_reference_v<decltype(e)>, "");

    std::lock_guard l{m_};
    if (hasReplayed_)
    {
        assert(0);
        return hasReplayed_;
    }

    JLOGV(
        j_.trace(), "InitialSync TicketCreateResult", jv("event", e.toJson()));

    if (needsOtherChainLastXChainTxn_)
    {
        if (auto f = federator_.lock())
        {
            // Inform the other sync object that the last transaction with a
            // result was found. e.dir_ is for the triggering transaction.
            Federator::ChainType const chainType = srcChainType(e.dir_);
            f->setLastXChainTxnWithResult(
                chainType, e.txnSeq_, 2, e.srcChainTxnHash_);
        }
        needsOtherChainLastXChainTxn_ = false;
    }

    if (!e.memoStr_.empty())
    {
        seenTriggeringTxns_.insert(e.txnHash_);
        if (lastXChainTxnWithResult_ && needsLastXChainTxn_)
        {
            if (e.txnHash_ == *lastXChainTxnWithResult_)
            {
                needsLastXChainTxn_ = false;
                JLOGV(
                    j_.trace(),
                    "InitialSync TicketCreateResult, found the trigger tx",
                    jv("txHash", e.txnHash_),
                    jv("chain_name",
                       (isMainchain_ ? "Mainchain" : "Sidechain")));
            }
        }
    }
    pendingEvents_[e.rpcOrder_] = std::move(e);

    if (canReplay(l))
        replay(l);

    return hasReplayed_;
}

bool
InitialSync::onEvent(event::DepositAuthResult&& e)
{
    return onResultEvent(std::move(e), 1);
}

bool
InitialSync::onEvent(event::BootstrapTicket&& e)
{
    std::lock_guard l{m_};

    JLOGV(j_.trace(), "InitialSync onBootstrapTicket", jv("event", e.toJson()));

    if (hasReplayed_)
    {
        assert(0);
        return hasReplayed_;
    }

    pendingEvents_[e.rpcOrder_] = std::move(e);

    if (canReplay(l))
        replay(l);

    return hasReplayed_;
}

bool
InitialSync::onEvent(event::DisableMasterKeyResult&& e)
{
    std::lock_guard l{m_};

    if (hasReplayed_)
    {
        assert(0);
        return hasReplayed_;
    }

    JLOGV(
        j_.trace(),
        "InitialSync onDisableMasterKeyResultEvent",
        jv("event", e.toJson()));
    assert(!disableMasterKeySeq_);
    disableMasterKeySeq_ = e.txnSeq_;

    pendingEvents_[e.rpcOrder_] = std::move(e);

    if (canReplay(l))
        replay(l);

    return hasReplayed_;
}

template <class T>
bool
InitialSync::onTriggerEvent(T&& e)
{
    static_assert(std::is_rvalue_reference_v<decltype(e)>, "");

    std::lock_guard l{m_};
    if (hasReplayed_)
    {
        assert(0);
        return hasReplayed_;
    }

    JLOGV(j_.trace(), "InitialSync onTriggerEvent", jv("event", e.toJson()));
    seenTriggeringTxns_.insert(e.txnHash_);
    if (lastXChainTxnWithResult_ && needsLastXChainTxn_)
    {
        if (e.txnHash_ == *lastXChainTxnWithResult_)
        {
            needsLastXChainTxn_ = false;
            JLOGV(
                j_.trace(),
                "InitialSync onTriggerEvent, found the trigger tx",
                jv("txHash", e.txnHash_),
                jv("chain_name", (isMainchain_ ? "Mainchain" : "Sidechain")));
        }
    }
    pendingEvents_[e.rpcOrder_] = std::move(e);

    if (canReplay(l))
    {
        replay(l);
    }
    return hasReplayed_;
}

template <class T>
bool
InitialSync::onResultEvent(T&& e, std::uint32_t seqTook)
{
    static_assert(std::is_rvalue_reference_v<decltype(e)>, "");

    std::lock_guard l{m_};
    if (hasReplayed_)
    {
        assert(0);
        return hasReplayed_;
    }

    JLOGV(j_.trace(), "InitialSync onResultEvent", jv("event", e.toJson()));

    if (needsOtherChainLastXChainTxn_)
    {
        if (auto f = federator_.lock())
        {
            // Inform the other sync object that the last transaction with a
            // result was found. e.dir_ is for the triggering transaction.
            Federator::ChainType const chainType = srcChainType(e.dir_);
            f->setLastXChainTxnWithResult(
                chainType, e.txnSeq_, seqTook, e.srcChainTxnHash_);
        }
        needsOtherChainLastXChainTxn_ = false;
    }

    pendingEvents_[e.rpcOrder_] = std::move(e);

    if (canReplay(l))
        replay(l);

    return hasReplayed_;
}

bool
InitialSync::onEvent(event::RefundTransferResult&& e)
{
    std::lock_guard l{m_};
    if (hasReplayed_)
    {
        assert(0);
    }
    else
    {
        pendingEvents_[e.rpcOrder_] = std::move(e);

        if (canReplay(l))
            replay(l);
    }
    return hasReplayed_;
}

bool
InitialSync::onEvent(event::StartOfHistoricTransactions&& e)
{
    std::lock_guard l{m_};
    if (lastXChainTxnWithResult_)
        LogicError("Initial sync could not find historic XChain transaction");

    if (needsOtherChainLastXChainTxn_)
    {
        if (auto f = federator_.lock())
        {
            // Inform the other sync object that the last transaction
            // with a result was found. Note that if start of historic
            // transactions is found while listening to the mainchain, the
            // _sidechain_ listener needs to be informed that there is no last
            // cross chain transaction with result.
            Federator::ChainType const chainType = getChainType(!isMainchain_);
            f->setNoLastXChainTxnWithResult(chainType);
        }
        needsOtherChainLastXChainTxn_ = false;
    }

    acquiringHistoricData_ = false;
    needsOtherChainLastXChainTxn_ = false;

    if (canReplay(l))
    {
        replay(l);
    }

    return hasReplayed_;
}

namespace detail {

Json::Value
getInfo(FederatorEvent const& event)
{
    return std::visit(
        [](auto const& e) {
            using eventType = decltype(e);
            Json::Value ret{Json::objectValue};
            if constexpr (std::is_same_v<
                              eventType,
                              event::XChainTransferDetected>)
            {
                ret[jss::type] = "xchain_transfer_detected";
                ret[jss::amount] = to_string(e.amt_);
                ret[jss::destination_account] = to_string(e.dst_);
                ret[jss::hash] = strHex(e.txnHash_);
                ret[jss::sequence] = e.txnSeq_;
                ret["rpc_order"] = e.rpcOrder_;
            }
            else if constexpr (std::is_same_v<
                                   eventType,
                                   event::XChainTransferResult>)
            {
                ret[jss::type] = "xchain_transfer_result";
                ret[jss::amount] = to_string(e.amt_);
                ret[jss::destination_account] = to_string(e.dst_);
                ret[jss::hash] = strHex(e.txnHash_);
                ret["triggering_tx_hash"] = strHex(e.triggeringTxnHash_);
                ret[jss::sequence] = e.txnSeq_;
                ret[jss::result] = transHuman(e.ter_);
                ret["rpc_order"] = e.rpcOrder_;
            }
            else
            {
                ret[jss::type] = "other_event";
            }
            return ret;
        },
        event);
}

}  // namespace detail

Json::Value
InitialSync::getInfo() const
{
    Json::Value ret{Json::objectValue};
    {
        std::lock_guard l{m_};
        ret["last_x_chain_txn_with_result"] = lastXChainTxnWithResult_
            ? strHex(*lastXChainTxnWithResult_)
            : "None";
        Json::Value triggerinTxns{Json::arrayValue};
        for (auto const& h : seenTriggeringTxns_)
        {
            triggerinTxns.append(strHex(h));
        }
        ret["seen_triggering_txns"] = triggerinTxns;
        ret["needs_last_x_chain_txn"] = needsLastXChainTxn_;
        ret["needs_other_chain_last_x_chain_txn"] =
            needsOtherChainLastXChainTxn_;
        ret["acquiring_historic_data"] = acquiringHistoricData_;
        ret["needs_replay_start_txn_hash"] = needsReplayStartTxnHash_;
    }
    return ret;
}

}  // namespace sidechain

}  // namespace ripple
