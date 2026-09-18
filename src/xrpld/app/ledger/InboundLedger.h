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
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

    // These are the reasons we might acquire a ledger
    enum class Reason {
        HISTORY,   // Acquiring past ledger
        GENERIC,   // Generic other reasons
        CONSENSUS  // We believe the consensus round requires this ledger
    };

    /**
     * How long to wait between retries, and so how long each timeout counted
     * against the acquisition takes. Long, since a ledger is worth chasing for
     * far longer than a consensus round.
     */
    static constexpr std::chrono::milliseconds kRetryInterval{3000};

    /**
     * @param app The application to run in.
     * @param hash The ledger to acquire.
     * @param seq Its sequence, or zero if not known yet.
     * @param reason Why it is being acquired.
     * @param clock The clock touch() records against.
     * @param peerSet Which peers to ask, and how to reach them.
     * @param retryInterval How long to wait between retries. Defaulted in
     *        production; InboundLedger_test passes a short one so a whole
     *        timeout chain runs in a fraction of the time. TimeoutCounter
     *        requires more than 10ms.
     */
    InboundLedger(
        Application& app,
        uint256 const& hash,
        std::uint32_t seq,
        Reason reason,
        clock_type& clock,
        std::unique_ptr<PeerSet> peerSet,
        std::chrono::milliseconds retryInterval = kRetryInterval);

    ~InboundLedger() override;

    // Called when another attempt is made to fetch this same ledger
    void
    update(std::uint32_t seq);

    /**
     * Returns true if we got all the data.
     */
    bool
    isComplete() const
    {
        return complete_;
    }

    /**
     * Returns false if we failed to get the data.
     */
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

    clock_type::time_point
    getLastAction() const
    {
        return lastAction_;
    }

protected:
    // Kept protected, with the two entry points naming it, so a test subclass (see
    // InboundLedger_test) can drive an acquisition the way the timer chain does, without routing
    // through the JobQueue. Production callers reach an acquisition through InboundLedgers.

    // Why trigger() is being run, which decides how deep a request goes and whether the
    // aggressive-retry branch is eligible.
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
     * Settle the acquisition and signal whatever is waiting on it. Runs at most
     * once. Call under mtx_, which the flags written here require.
     */
    void
    done();

private:
    void
    filterNodes(std::vector<std::pair<SHAMapNodeID, uint256>>& nodes, TriggerReason reason);

    std::vector<neededHash_t>
    getNeededHashes();

    void
    addPeers();

    void
    tryDB(node_store::Database& srcDB);

    /**
     * Whether either map of the ledger being acquired has been found
     * invalid.
     *
     * A walk returns a bare list of hashes, so an empty result does not
     * tell a satisfied map from an abandoned one; callers that read
     * emptiness as "nothing left to fetch" must ask this first. Asked
     * regardless of this acquisition's own flags after the one walk that
     * runs with the lock released, whose verdict can land after another
     * thread has reported the ledger complete. See SHAMap::addKnownNode
     * for why the verdict is final.
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

    void
    receiveNode(
        std::shared_ptr<Peer> const& peer,
        protocol::TMLedgerData const& packet,
        SHAMapAddNode& san);

    bool
    takeTxRootNode(std::string_view data, SHAMapAddNode& san);

    bool
    takeAsRootNode(std::string_view data, SHAMapAddNode& san);

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
