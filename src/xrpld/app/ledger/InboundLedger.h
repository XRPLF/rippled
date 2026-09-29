#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
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
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {

// A ledger we are trying to acquire
class InboundLedger : public TimeoutCounter,
                      public std::enable_shared_from_this<InboundLedger>,
                      public CountedObject<InboundLedger>
{
public:
    using ClockType = beast::AbstractClock<std::chrono::steady_clock>;

    // These are the reasons we might acquire a ledger
    enum class Reason {
        HISTORY,   // Acquiring past ledger
        GENERIC,   // Generic other reasons
        CONSENSUS  // We believe the consensus round requires this ledger
    };

    /**
     * How long to wait between retries, and so how long one timeout takes.
     */
    static constexpr std::chrono::milliseconds kRetryInterval{3000};

    /**
     * @param app The application to run in.
     * @param hash The ledger to acquire.
     * @param seq Its sequence, or zero if not known yet.
     * @param reason Why it is being acquired.
     * @param clock The clock touch() records against.
     * @param peerSet Which peers to ask, and how to reach them.
     * @param retryInterval How long to wait between retries. TimeoutCounter
     *        requires more than 10ms and less than 30s.
     */
    InboundLedger(
        Application& app,
        UInt256 const& hash,
        std::uint32_t seq,
        Reason reason,
        ClockType& clock,
        std::unique_ptr<PeerSet> peerSet,
        std::chrono::milliseconds retryInterval = kRetryInterval);

    ~InboundLedger() override;

    // Called when another attempt is made to fetch this same ledger
    void
    update(std::uint32_t seq);

    /**
     * Whether the acquisition succeeded and its ledger has been settled. Every
     * path that sets this settles the ledger first, so a caller that sees it
     * may use the ledger directly.
     *
     * @return Whether the ledger is complete and settled.
     */
    bool
    isComplete() const
    {
        return complete_;
    }

    /**
     * @return Whether the acquisition has failed.
     */
    bool
    isFailed() const
    {
        return failed_;
    }

    /**
     * The acquired ledger.
     *
     * A failed acquisition may still hold a partially built ledger, which
     * getJson() reports on, so ledger_ is kept while this answers nullptr.
     *
     * @return The ledger, or nullptr before a header is obtained and once the
     *         acquisition has failed.
     */
    std::shared_ptr<Ledger const>
    getLedger() const
    {
        return failed_ ? nullptr : ledger_;
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

    using NeededHashT = std::pair<protocol::TMGetObjectByHash::ObjectType, UInt256>;

    /**
     * Return a json::ValueType::Object.
     */
    json::Value
    getJson(int);

    void
    runData();

    void
    touch()
    {
        lastAction_ = clock_.now();
    }

    ClockType::time_point
    getLastAction() const
    {
        return lastAction_;
    }

protected:
    // Protected so a test subclass can drive an acquisition through trigger() and done().

    // Why trigger() is being run, which decides how deep a request goes and whether an
    // aggressive retry applies.
    enum class TriggerReason { Added, Reply, Timeout };

    /**
     * Ask for more nodes, or judge what has been collected.
     *
     * @param peer The peer to ask, or nullptr to ask everyone being tracked.
     * @param reason Why the acquisition is being triggered.
     */
    void
    trigger(std::shared_ptr<Peer> const& peer, TriggerReason reason);

    /**
     * Settle the acquisition, publish its outcome, and signal whatever is
     * waiting on it. Runs at most once. Call under mtx_, which the flags
     * written here require. Settles before publishing, since isComplete() is
     * read without mtx_.
     */
    void
    done();

private:
    void
    filterNodes(std::vector<std::pair<SHAMapNodeID, UInt256>>& nodes, TriggerReason reason);

    std::vector<NeededHashT>
    getNeededHashes();

    void
    addPeers();

    void
    tryDB(node_store::Database& srcDB);

    /**
     * Whether either map of the ledger being acquired has been found invalid.
     *
     * A walk returns a bare list of hashes, so this is what tells a satisfied
     * map from an abandoned one. Callers ask it before reading emptiness as
     * "nothing left to fetch". See SHAMap::addKnownNode for why the verdict is
     * final.
     *
     * @return Whether either map is Invalid, and false while there is no ledger
     *         yet, since then there is no map to judge.
     */
    [[nodiscard]] bool
    hasInvalidMap() const;

    void
    onTimer(bool progress, ScopedLockType& sl) override;

    std::size_t
    getPeerCount() const;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    int
    processData(std::shared_ptr<Peer> peer, protocol::TMLedgerData const& data);

    bool
    takeHeader(std::string_view data);

    /**
     * Fail the acquisition when the header's account hash is zero. No ledger
     * has an empty state map, so such a header cannot name a ledger. Both
     * tryDB() and takeHeader() judge the header here.
     *
     * @return Whether the acquisition was failed. Then failed_ is set and
     *         ledger_ is null.
     */
    bool
    failOnZeroAccountHash();

    void
    receiveNode(
        std::shared_ptr<Peer> const& peer,
        protocol::TMLedgerData const& packet,
        SHAMapAddNode& san);

    bool
    takeTxRootNode(std::string_view data, SHAMapAddNode& san);

    bool
    takeAsRootNode(std::string_view data, SHAMapAddNode& san);

    std::vector<UInt256>
    neededTxHashes(int max, SHAMapSyncFilter const* filter) const;

    std::vector<UInt256>
    neededStateHashes(int max, SHAMapSyncFilter const* filter) const;

    ClockType& clock_;
    ClockType::time_point lastAction_;

    std::shared_ptr<Ledger> ledger_;
    bool haveHeader_{false};
    bool haveState_{false};
    bool haveTransactions_{false};
    bool signaled_{false};
    bool byHash_{true};
    std::uint32_t seq_;
    Reason const reason_;

    std::set<UInt256> recentNodes_;

    SHAMapAddNode stats_;

    // Data we have received from peers
    std::mutex receivedDataLock_;
    std::vector<std::pair<std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData>>>
        receivedData_;
    bool receiveDispatched_{false};
    std::unique_ptr<PeerSet> peerSet_;
};

}  // namespace xrpl
