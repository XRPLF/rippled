#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

// A ledger we are trying to acquire
class InboundLedger final : public TimeoutCounter,
                            public std::enable_shared_from_this<InboundLedger>,
                            public CountedObject<InboundLedger>
{
public:
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

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

    ~InboundLedger() override;

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
        return ledger_;
    }

    std::uint32_t
    getSeq() const
    {
        return seq_;
    }

    bool
    checkLocal();
    void
    init(ScopedLockType& collectionLock);

    bool
    gotData(std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData> const&);

    using neededHash_t = std::pair<protocol::TMGetObjectByHash::ObjectType, uint256>;

    /** Return a json::ValueType::Object. */
    json::Value
    getJson(int);

    void
    runData();

    void
    touch()
    {
        lastAction_ = clock_.now();
    }

    clock_type::time_point
    getLastAction() const
    {
        return lastAction_;
    }

private:
    enum class TriggerReason { Added, Reply, Timeout };

    void
    filterNodes(std::vector<std::pair<SHAMapNodeID, uint256>>& nodes, TriggerReason reason);

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
    processData(std::shared_ptr<Peer> peer, protocol::TMLedgerData const& data);

    bool
    takeHeader(std::string const& data);

    void
    receiveNode(protocol::TMLedgerData const& packet, SHAMapAddNode&);

    bool
    takeTxRootNode(Slice const& data, SHAMapAddNode&);

    bool
    takeAsRootNode(Slice const& data, SHAMapAddNode&);

    std::vector<uint256>
    neededTxHashes(int max, SHAMapSyncFilter const* filter) const;

    std::vector<uint256>
    neededStateHashes(int max, SHAMapSyncFilter const* filter) const;

    clock_type& clock_;
    clock_type::time_point lastAction_;

    std::shared_ptr<Ledger> ledger_;
    bool haveHeader_{false};
    bool haveState_{false};
    bool haveTransactions_{false};
    bool signaled_{false};
    bool byHash_{true};
    std::uint32_t seq_;
    Reason const reason_;

    std::set<uint256> recentNodes_;

    SHAMapAddNode stats_;

    // Data we have received from peers
    std::mutex receivedDataLock_;
    std::vector<std::pair<std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData>>>
        receivedData_;
    bool receiveDispatched_{false};
    std::unique_ptr<PeerSet> peerSet_;
};

}  // namespace xrpl
