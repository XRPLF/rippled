#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Scheduler.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xrpl::node_store {

/* This class keeps a ring of append-only key-value Backend objects (generations)
 * for persisting SHAMap records, to facilitate online deletion of data. New nodes
 * are written to the newest (writable) generation; reads probe newest -> oldest.
 * Rather than copying the entire live state into a fresh backend every rotation
 * (O(total state)), a generation is dropped only once its still-live nodes have been
 * evacuated forward, so a cold node is re-stored ~once per ring cycle instead of every
 * rotation (O(churn)).
 */

class DatabaseRotating : public Database
{
public:
    // Receives the full generation ring, ordered oldest -> newest, to persist durably.
    using RingPersist = std::function<void(std::vector<std::string> const& generations)>;

    DatabaseRotating(
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal)
        : Database(scheduler, readThreads, config, journal)
    {
    }

    /**
     * Append a fresh writable generation. The prior writable becomes a sealed,
     * read-only generation that remains in the ring (still served by reads).
     *
     * @param newWritable The new (empty) writable backend.
     * @param persist Executed after the push, outside the lock, with the full ring
     *        (oldest -> newest) so the caller can durably record it.
     */
    virtual void
    advance(std::unique_ptr<Backend>&& newWritable, RingPersist const& persist) = 0;

    /**
     * Number of live generations currently in the ring.
     */
    virtual std::size_t
    generationCount() const = 0;

    /**
     * Number of live nodes copied forward out of the retiring generation during the
     * current retire window (reset by beginRetire). This is the evacuation volume — the
     * churn the ring pays in place of copying the whole live state every rotation — so it
     * quantifies that reclamation is O(churn) rather than O(total state).
     */
    virtual std::uint64_t
    copyForwardCount() const = 0;

    /**
     * Begin/end retiring the oldest generation. While a retire is in progress, any
     * read served by the retiring generation is copied forward into the writable
     * backend (even ordinary reads): that generation is about to be dropped, so its
     * still-live nodes must be preserved. Copy-forward is scoped to the retiring
     * generation only — reads served by other sealed generations are NOT copied, which
     * is what keeps evacuation O(churn) rather than O(total state).
     */
    virtual void
    beginRetire() = 0;
    virtual void
    endRetire() = 0;

    /**
     * Drop the oldest generation (its survivors already evacuated during the retire
     * window). The generation's directory is deleted only after @p persist records the
     * shortened ring, so a crash never leaves a persisted name without its backend.
     */
    virtual void
    retireOldest(RingPersist const& persist) = 0;
};

}  // namespace xrpl::node_store
