#ifndef XRPL_SHAMAP_FAMILY_H_INCLUDED
#define XRPL_SHAMAP_FAMILY_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <cstdint>

namespace ripple {

class Family
{
public:
    Family(Family const&) = delete;
    Family(Family&&) = delete;

    Family&
    operator=(Family const&) = delete;

    Family&
    operator=(Family&&) = delete;

    explicit Family() = default;
    virtual ~Family() = default;

    virtual NodeStore::Database&
    db() = 0;

    virtual NodeStore::Database const&
    db() const = 0;

    virtual beast::Journal const&
    journal() = 0;

    /** Return a pointer to the Family Full Below Cache */
    virtual std::shared_ptr<FullBelowCache>
    getFullBelowCache() = 0;

    /** Return a pointer to the Family Tree Node Cache */
    virtual std::shared_ptr<TreeNodeCache>
    getTreeNodeCache() = 0;

    virtual void
    sweep() = 0;

    /** Acquire ledger that has a missing node by ledger sequence
     *
     * @param refNum Sequence of ledger to acquire.
     * @param nodeHash Hash of missing node to report in throw.
     */
    virtual void
    missingNodeAcquireBySeq(std::uint32_t refNum, uint256 const& nodeHash) = 0;

    /** Acquire ledger that has a missing node by ledger hash
     *
     * @param refHash Hash of ledger to acquire.
     * @param refNum Ledger sequence with missing node.
     */
    virtual void
    missingNodeAcquireByHash(uint256 const& refHash, std::uint32_t refNum) = 0;

    virtual void
    reset() = 0;
};

}  // namespace ripple

#endif
