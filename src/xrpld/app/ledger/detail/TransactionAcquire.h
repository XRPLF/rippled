#ifndef XRPL_APP_LEDGER_TRANSACTIONACQUIRE_H_INCLUDED
#define XRPL_APP_LEDGER_TRANSACTIONACQUIRE_H_INCLUDED

#include <xrpld/overlay/PeerSet.h>

#include <xrpl/shamap/SHAMap.h>

namespace ripple {

// VFALCO TODO rename to PeerTxRequest
// A transaction set we are trying to acquire
class TransactionAcquire final
    : public TimeoutCounter,
      public std::enable_shared_from_this<TransactionAcquire>,
      public CountedObject<TransactionAcquire>
{
public:
    using pointer = std::shared_ptr<TransactionAcquire>;

    TransactionAcquire(
        Application& app,
        uint256 const& hash,
        std::unique_ptr<PeerSet> peerSet);
    ~TransactionAcquire() = default;

    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, Slice>> const& data,
        std::shared_ptr<Peer> const&);

    void
    init(int startPeers);

    void
    stillNeed();

private:
    std::shared_ptr<SHAMap> mMap;
    bool mHaveRoot;
    std::unique_ptr<PeerSet> mPeerSet;

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

}  // namespace ripple

#endif
