//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_INBOUNDLEDGER_H_INCLUDED
#define RIPPLE_APP_LEDGER_INBOUNDLEDGER_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>

#include <mutex>
#include <set>
#include <utility>

namespace ripple {

// A ledger we are trying to acquire
class InboundLedger final : public TimeoutCounter,
                            public std::enable_shared_from_this<InboundLedger>,
                            public CountedObject<InboundLedger>
{
public:
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

    // These are the reasons we might acquire a ledger
    enum class Reason {
        HISTORY,   // Acquiring past ledger
        GENERIC,   // Generic other reasons
        CONSENSUS  // We believe the consensus round requires this ledger
    };

    InboundLedger(
        Application& app,
        uint256 const& hash,
        std::uint32_t seq,
        Reason reason,
        clock_type&,
        std::unique_ptr<PeerSet> peerSet);

    ~InboundLedger();

    // Called when another attempt is made to fetch this same ledger
    void
    update(std::uint32_t seq);

    /** Returns true if we got all the data. */
    bool
    isComplete() const
    {
        return complete_;
    }

    /** Returns false if we failed to get the data. */
    bool
    isFailed() const
    {
        return failed_;
    }

    std::shared_ptr<Ledger const>
    getLedger() const
    {
        return mLedger;
    }

    std::uint32_t
    getSeq() const
    {
        return mSeq;
    }

    bool
    checkLocal();
    void
    init(ScopedLockType& collectionLock);

    bool
    gotData(
        std::weak_ptr<Peer>,
        std::shared_ptr<protocol::TMLedgerData> const&);

    using neededHash_t =
        std::pair<protocol::TMGetObjectByHash::ObjectType, uint256>;

    /** Return a Json::objectValue. */
    Json::Value
    getJson(int);

    void
    runData();

    void
    touch()
    {
        mLastAction = m_clock.now();
    }

    clock_type::time_point
    getLastAction() const
    {
        return mLastAction;
    }

private:
    enum class TriggerReason { added, reply, timeout };

    void
    filterNodes(
        std::vector<std::pair<SHAMapNodeID, uint256>>& nodes,
        TriggerReason reason);

    void
    trigger(std::shared_ptr<Peer> const&, TriggerReason);

    std::vector<neededHash_t>
    getNeededHashes();

    void
    addPeers();

    void
    tryDB(NodeStore::Database& srcDB);

    void
    done();

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::size_t
    getPeerCount() const;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    int
    processData(std::shared_ptr<Peer> peer, protocol::TMLedgerData& data);

    bool
    takeHeader(std::string const& data);

    void
    receiveNode(protocol::TMLedgerData& packet, SHAMapAddNode&);

    bool
    takeTxRootNode(Slice const& data, SHAMapAddNode&);

    bool
    takeAsRootNode(Slice const& data, SHAMapAddNode&);

    std::vector<uint256>
    neededTxHashes(int max, SHAMapSyncFilter* filter) const;

    std::vector<uint256>
    neededStateHashes(int max, SHAMapSyncFilter* filter) const;

    clock_type& m_clock;
    clock_type::time_point mLastAction;

    std::shared_ptr<Ledger> mLedger;
    bool mHaveHeader;
    bool mHaveState;
    bool mHaveTransactions;
    bool mSignaled;
    bool mByHash;
    std::uint32_t mSeq;
    Reason const mReason;

    std::set<uint256> mRecentNodes;

    SHAMapAddNode mStats;

    // Data we have received from peers
    std::mutex mReceivedDataLock;
    std::vector<
        std::pair<std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData>>>
        mReceivedData;
    bool mReceiveDispatched;
    std::unique_ptr<PeerSet> mPeerSet;
};

}  // namespace ripple

#endif
