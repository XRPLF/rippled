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

#ifndef RIPPLE_SIDECHAIN_IMPL_INITIALSYNC_H_INCLUDED
#define RIPPLE_SIDECHAIN_IMPL_INITIALSYNC_H_INCLUDED

#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/basics/ThreadSaftyAnalysis.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/utility/Journal.h>

#include <map>

namespace ripple {
namespace sidechain {

class Federator;
class WebsocketClient;

// This class handles the logic of getting a federator that joins the network
// into a "normal" state of handling new cross chain transactions and results.
// There will be two instance of this class, one for the main chain and one for
// the side chain.
//
// When a federator joins the network of other federators, the network can be in
// one of three states:
//
// 1) The initial sidechain startup.
// 2) Running normally with a quorum of federators. This federator that's
//    joining just increases the quorum.
// 3) A stalled sidechain without enough federators to make forward progress.
//    This federator may or may not increase the quorum enough so cross chain
//    transactions can continue. In the meantime, cross chain transactions may
//    continue to accumulate.
//
// No matter the state of the federators network, connecting to the network goes
// through the same steps. There are two instances of this class, one
// for the main chain and one for the side chain.
//
// The RPC command used to fetch transactions will initially be configured to
// retrieve both historical transactions and new transactions. Once the
// information needed from the historical transactions are retrieved, it will be
// changed to only stream new transactions.
//
// There are two states this class can be in: pre-replay and post-replay. In
// pre-replay mode, the class collects information from both historic and
// transactions that will be used for helping the this instance and the "other"
// instance of this class known when to stop collecting historic data, as well
// as collecting transactions for replaying.
//
// Historic data needs to be collected until:
//
// 1) The most recent historic `XChainTransferResult` event is detected (or the
// account's first transaction is detected). This is used to inform the "other"
// instance of this class which `XChainTransferDetected` event is the first that
// may need to be replayed. Since the previous `XChainTransferDetected` events
// have results on the other chain, we can definitively say the federators have
// handled these events and they don't need to be replayed.
//
// 2) Once the `lastXChainTxnWithResult_` is know, historic transactions need be
// acquired until that transaction is seen on a `XChainTransferDetected` event.
//
// Once historic data collection has completed, the collected transactions are
// replayed to the federator, and this class is not longer needed. All new
// transactions should simply be forwarded to the federator.
//
class InitialSync
{
private:
    std::weak_ptr<Federator> federator_;
    // Holds all the eventsseen so far. These events will be replayed to the
    // federator upon switching to `normal` mode. Will be cleared while
    // replaying.
    std::map<std::int32_t, FederatorEvent> GUARDED_BY(m_) pendingEvents_;
    // Holds all triggering cross chain transactions seen so far. This is used
    // to determine if the `XChainTransferDetected` event with the
    // `lastXChainTxnWithResult_` has has been seen or not. Will be cleared
    // while replaying
    hash_set<uint256> GUARDED_BY(m_) seenTriggeringTxns_;
    // Hash of the last cross chain transaction on this chain with a result on
    // the "other" chain. Note: this is set when the `InitialSync` for the
    // "other" chain encounters the transaction.
    std::optional<uint256> GUARDED_BY(m_) lastXChainTxnWithResult_;
    // Track if we need to keep acquiring historic transactions for the
    // `lastXChainTxnWithResult_`. This is true if the lastXChainTxnWithResult_
    // is unknown, or it is known and the transaction is not part of that
    // collection yet.
    bool GUARDED_BY(m_) needsLastXChainTxn_{true};
    // Track if we need to keep acquiring historic transactions for the other
    // chain's `lastXChainTxnWithResult_` hash value. This is true if no
    // cross chain transaction results are known and the first historical
    // transaction has not been encountered.
    bool GUARDED_BY(m_) needsOtherChainLastXChainTxn_{true};
    // Track if the transaction to start the replay from is known. This is true
    // until `lastXChainTxnWithResult_` is known and the other listener has not
    // encountered the first historical transaction
    bool GUARDED_BY(m_) needsReplayStartTxnHash_{true};
    // True if the historical transactions have been replayed to the federator
    bool GUARDED_BY(m_) hasReplayed_{false};
    // Track the state of the transaction data we are acquiring.
    // If this is `false`, only new transactions events will be streamed.
    // Note: there will be a period where this is `false` but historic txns will
    // continue to come in until the rpc command has responded to the request to
    // shut off historic data.
    bool GUARDED_BY(m_) acquiringHistoricData_{true};
    // All transactions before "DisableMasterKey" are setup transactions and
    // should be ignored
    std::optional<std::uint32_t> GUARDED_BY(m_) disableMasterKeySeq_;
    bool const isMainchain_;
    mutable std::mutex m_;
    beast::Journal j_;
    // See description on class for explanation of states

public:
    InitialSync(
        std::weak_ptr<Federator> federator,
        bool isMainchain,
        beast::Journal j);

    // Return `hasReplayed_`. This is used to determine if events should
    // continue to be routed to this object. Once replayed, events can be
    // processed normally.
    [[nodiscard]] bool
    setLastXChainTxnWithResult(uint256 const& hash) EXCLUDES(m_);

    // There have not been any cross chain transactions.
    // Return `hasReplayed_`. This is used to determine if events should
    // continue to be routed to this object. Once replayed, events can be
    // processed normally.
    [[nodiscard]] bool
    setNoLastXChainTxnWithResult() EXCLUDES(m_);

    // Return `hasReplayed_`. This is used to determine if events should
    // continue to be routed to this object. Once replayed, events can be
    // processed normally.
    [[nodiscard]] bool
    onEvent(event::XChainTransferDetected&& e) EXCLUDES(m_);

    // Return `hasReplayed_`. This is used to determine if events should
    // continue to be routed to this object. Once replayed, events can be
    // processed normally.
    [[nodiscard]] bool
    onEvent(event::XChainTransferResult&& e) EXCLUDES(m_);

    // Return `hasReplayed_`. This is used to determine if events should
    // continue to be routed to this object. Once replayed, events can be
    // processed normally.
    [[nodiscard]] bool
    onEvent(event::RefundTransferResult&& e) EXCLUDES(m_);

    // Return `hasReplayed_`. This is used to determine if events should
    // continue to be routed to this object. Once replayed, events can be
    // processed normally.
    [[nodiscard]] bool
    onEvent(event::StartOfHistoricTransactions&& e) EXCLUDES(m_);
    // Return `hasReplayed_`.
    [[nodiscard]] bool
    onEvent(event::TicketCreateTrigger&& e) EXCLUDES(m_);
    // Return `hasReplayed_`.
    [[nodiscard]] bool
    onEvent(event::TicketCreateResult&& e) EXCLUDES(m_);
    // Return `hasReplayed_`.
    [[nodiscard]] bool
    onEvent(event::DepositAuthResult&& e) EXCLUDES(m_);
    [[nodiscard]] bool
    onEvent(event::BootstrapTicket&& e) EXCLUDES(m_);
    [[nodiscard]] bool
    onEvent(event::DisableMasterKeyResult&& e) EXCLUDES(m_);

    Json::Value
    getInfo() const EXCLUDES(m_);

private:
    // Replay when historical transactions are no longer being acquired,
    // and the transaction to start the replay from is known.
    bool
    canReplay(std::lock_guard<std::mutex> const&) const REQUIRES(m_);
    void
    replay(std::lock_guard<std::mutex> const&) REQUIRES(m_);
    bool
    hasTransaction(uint256 const& txnHash, std::lock_guard<std::mutex> const&)
        const REQUIRES(m_);
    void
    stopHistoricalTxns(std::lock_guard<std::mutex> const&) REQUIRES(m_);
    template <class T>
    bool
    onTriggerEvent(T&& e);
    template <class T>
    bool
    onResultEvent(T&& e, std::uint32_t seqTook);
    void
    done();
};

}  // namespace sidechain
}  // namespace ripple

#endif
