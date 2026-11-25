#pragma once

#include <xrpl/protocol/Protocol.h>

#include <condition_variable>
#include <map>
#include <mutex>

namespace xrpl {

/**
 * Keeps track of which ledgers haven't been fully saved.
 *
 * During the ledger building process this collection will keep
 * track of those ledgers that are being built but have not yet
 * been completely written.
 */
class PendingSaves
{
private:
    std::mutex mutable mutex_;
    std::map<LedgerIndex, bool> map_;
    std::condition_variable await_;

public:
    /**
     * Start working on a ledger
     *
     * This is called prior to updating the SQLite indexes.
     *
     * @param seq The sequence of the ledger to save.
     * @return 'true' if work should be done
     */
    bool
    startWork(LedgerIndex seq);

    /**
     * Finish working on a ledger
     *
     * This is called after updating the SQLite indexes.
     * The tracking of the work in progress is removed and
     * threads awaiting completion are notified.
     *
     * @param seq The sequence of the ledger that was saved.
     */
    void
    finishWork(LedgerIndex seq);

    /**
     * Return `true` if a ledger is in the progress of being saved.
     *
     * @param seq The sequence of the ledger to look up.
     */
    bool
    pending(LedgerIndex seq);

    /**
     * Schedule the save of a ledger, unless one is scheduled or in progress.
     *
     * Called before an asynchronous save is dispatched. A synchronous caller
     * uses claimWork() instead.
     *
     * @param seq The sequence of the ledger to save.
     * @return Whether the caller should dispatch the save.
     */
    bool
    shouldWork(LedgerIndex seq);

    /**
     * Claim the save of a ledger for the caller.
     *
     * Waits while another thread is saving the sequence, then marks it as in
     * progress for the caller. Where shouldWork() followed by startWork() lets
     * another thread take the work between the two calls, this claim happens
     * under one lock, so the caller always owns the save when it returns. It
     * must call finishWork() afterwards.
     *
     * @param seq The sequence of the ledger to save.
     */
    void
    claimWork(LedgerIndex seq);

    /**
     * Get a snapshot of the pending saves
     *
     * Each entry in the returned map corresponds to a ledger
     * that is in progress or dispatched. The boolean indicates
     * whether work is currently in progress.
     *
     * @return A copy of the pending saves.
     */
    std::map<LedgerIndex, bool>
    getSnapshot() const;
};

}  // namespace xrpl
