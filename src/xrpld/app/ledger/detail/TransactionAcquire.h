#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl {

// VFALCO TODO rename to PeerTxRequest
// A transaction set we are trying to acquire
class TransactionAcquire final : public TimeoutCounter,
                                 public std::enable_shared_from_this<TransactionAcquire>,
                                 public CountedObject<TransactionAcquire>
{
public:
    using pointer = std::shared_ptr<TransactionAcquire>;

    TransactionAcquire(Application& app, uint256 const& hash, std::unique_ptr<PeerSet> peerSet);
    ~TransactionAcquire() override = default;

    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, Slice>> const& data,
        std::shared_ptr<Peer> const&);

    void
    init(int startPeers);

    void
    stillNeed();

private:
    std::shared_ptr<SHAMap> map_;
    bool haveRoot_{false};
    std::unique_ptr<PeerSet> peerSet_;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    void
    done();

    void
    addPeers(std::size_t limit);

    void
    trigger(std::shared_ptr<Peer> const&);
    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;
};

}  // namespace xrpl
