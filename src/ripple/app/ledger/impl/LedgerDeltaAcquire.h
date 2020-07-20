//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_LEDGERDELTAACQUIRE_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERDELTAACQUIRE_H_INCLUDED

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/base_uint.h>

#include <list>
#include <map>

namespace ripple {
class InboundLedgers;
class PeerSet;
namespace test {
class LedgerReplayClient;
}  // namespace test

/**
 * Manage the retrieval of a ledger delta (header and transactions)
 * from the network. Before asking peers, always check if the local
 * node has the ledger.
 */
class LedgerDeltaAcquire final
    : public TimeoutCounter,
      public std::enable_shared_from_this<LedgerDeltaAcquire>,
      public CountedObject<LedgerDeltaAcquire>
{
public:
    /**
     * A callback used to notify that the delta's data is ready or failed.
     * @param successful  if the ledger delta data was acquired successfully
     * @param hash  hash of the ledger to build
     */
    using OnDeltaDataCB =
        std::function<void(bool successful, uint256 const& hash)>;

    /**
     * Constructor
     * @param app  Application reference
     * @param inboundLedgers  InboundLedgers reference
     * @param ledgerHash  hash of the ledger
     * @param ledgerSeq  sequence number of the ledger
     * @param peerSet  manage a set of peers that we will ask for the ledger
     */
    LedgerDeltaAcquire(
        Application& app,
        InboundLedgers& inboundLedgers,
        uint256 const& ledgerHash,
        std::uint32_t ledgerSeq,
        std::unique_ptr<PeerSet> peerSet);

    ~LedgerDeltaAcquire() override;

    /**
     * Start the LedgerDeltaAcquire task
     * @param numPeers  number of peers to try initially
     */
    void
    init(int numPeers);

    /**
     * Process the data extracted from a peer's reply
     * @param info  info (header) of the ledger
     * @param orderedTxns  set of Txns of the ledger
     *
     * @note info and Txns must have been verified against the ledger hash
     */
    void
    processData(
        LedgerInfo const& info,
        std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns);

    /**
     * Try to build the ledger if not already
     * @param parent  parent ledger
     * @return  the ledger if built, nullptr otherwise (e.g. waiting for peers'
     *          replies of the ledger info (header) and Txns.)
     * @note may throw runtime_error if the replay failed due to data error
     */
    std::shared_ptr<Ledger const>
    tryBuild(std::shared_ptr<Ledger const> const& parent);

    /**
     * Add a reason and a callback to the LedgerDeltaAcquire subtask.
     * The reason is used to process the ledger once it is replayed.
     * The callback is called when the delta's data is ready or failed
     * @note the callback will be called once and only once unless this object
     *       is destructed before the call.
     */
    void
    addDataCallback(InboundLedger::Reason reason, OnDeltaDataCB&& cb);

    static char const*
    getCountedObjectName()
    {
        return "LedgerDeltaAcquire";
    }

private:
    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    /**
     * Trigger another round
     * @param limit  number of new peers to send the request
     * @param sl  lock. this function must be called with the lock
     */
    void
    trigger(std::size_t limit, ScopedLockType& sl);

    /**
     * Process a newly built ledger, such as store it.
     * @param sl  lock. this function must be called with the lock
     * @param reason  specific new reason if any
     * @note this function should be called (1) when the ledger is built the
     *       first time, and (2) when a LedgerReplayTask with a new reason
     *       is added.
     */
    void
    onLedgerBuilt(
        ScopedLockType& sl,
        std::optional<InboundLedger::Reason> reason = {});

    /**
     * Call the OnDeltaDataCB callbacks
     * @param sl  lock. this function must be called with the lock
     */
    void
    notify(ScopedLockType& sl);

    InboundLedgers& inboundLedgers_;
    std::uint32_t const ledgerSeq_;
    std::unique_ptr<PeerSet> peerSet_;
    std::shared_ptr<Ledger const> replayTemp_ = {};
    std::shared_ptr<Ledger const> fullLedger_ = {};
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns_;
    std::vector<OnDeltaDataCB> dataReadyCallbacks_;
    std::set<InboundLedger::Reason> reasons_;
    std::uint32_t noFeaturePeerCount = 0;
    bool fallBack_ = false;

    friend class LedgerReplayTask;  // for asserts only
    friend class test::LedgerReplayClient;
};

}  // namespace ripple

#endif
