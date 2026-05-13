#pragma once

#include <xrpl/protocol/Protocol.h>

#include <condition_variable>
#include <map>
#include <mutex>

namespace xrpl {

/** Coordination primitive tracking validated ledgers not yet fully written to
 *  the SQLite relational database.
 *
 *  When a validated ledger is being persisted, there is a window in which it
 *  exists in memory but its index entries are incomplete on disk. Any code that
 *  reports the "validated range" of ledgers to peers or clients must exclude
 *  these in-progress sequences; otherwise it could direct a requester to query
 *  a partially-written row.
 *
 *  ## Internal state machine
 *
 *  The internal map encodes three observable states per ledger sequence:
 *
 *  | Map state                  | Meaning                                    |
 *  |----------------------------|--------------------------------------------|
 *  | key absent                 | Not pending; safe for DB queries           |
 *  | key present, value `false` | Registered/dispatched, write not started   |
 *  | key present, value `true`  | A thread is actively writing to SQLite     |
 *
 *  The canonical "finished" state is key-absent; `finishWork()` erases the
 *  entry (rather than resetting the flag) so that `pending()` and the blocking
 *  loop in `shouldWork()` use absence as the termination condition.
 *
 *  ## Typical call sequence
 *
 *  1. `pendSaveValidated()` calls `shouldWork(seq, isSynchronous)` to either
 *     claim a fresh entry or "steal" a registered-but-unstarted one.
 *  2. `saveValidatedLedger()` calls `startWork(seq)` to atomically flip the
 *     flag from `false` → `true`. A `false` return means another thread won
 *     the race; the caller logs "Save aborted" and exits early.
 *  3. `saveValidatedLedger()` calls `finishWork(seq)` after the DB write
 *     completes, waking any synchronous waiters.
 *  4. `LedgerMaster::getValidatedRange()` calls `getSnapshot()` to trim the
 *     reported min/max validated range, excluding any in-progress sequences.
 *
 *  This class is a pure coordination primitive. It does not own a thread pool
 *  or `JobQueue`; all scheduling policy lives in `pendSaveValidated()`.
 *
 *  @note Thread-safe. All methods acquire `mutex_` internally. The synchronous
 *      blocking path in `shouldWork()` re-acquires the lock after each
 *      `await_.wait()` and re-checks in a loop because `notify_all()` can
 *      wake multiple waiters simultaneously.
 */
class PendingSaves
{
private:
    std::mutex mutable mutex_;
    std::map<LedgerIndex, bool> map_;
    std::condition_variable await_;

public:
    /** Atomically claim the right to begin writing a ledger to the database.
     *
     *  Flips the map entry for @p seq from `false` to `true`, signalling that
     *  a thread is actively writing to SQLite. This must be called after
     *  `shouldWork()` returns `true` and before the DB write begins.
     *
     *  @param seq Ledger sequence number to claim.
     *  @return `true` if this caller successfully claimed the write; `false` if
     *      the entry is absent (write already completed) or already `true`
     *      (another thread started it first). A `false` return is the caller's
     *      signal to abort with a "Save aborted" log and return early.
     */
    bool
    startWork(LedgerIndex seq)
    {
        std::scoped_lock const lock(mutex_);

        auto it = map_.find(seq);

        if ((it == map_.end()) || it->second)
        {
            // Work done or another thread is doing it
            return false;
        }

        it->second = true;
        return true;
    }

    /** Mark a ledger's database write as complete and wake any waiters.
     *
     *  Erases the entry for @p seq from the map — key-absent is the canonical
     *  "done" state — then calls `notify_all()` so any synchronous caller
     *  blocked in `shouldWork()` can re-evaluate.
     *
     *  @param seq Ledger sequence number whose write has completed.
     */
    void
    finishWork(LedgerIndex seq)
    {
        std::scoped_lock const lock(mutex_);

        map_.erase(seq);
        await_.notify_all();
    }

    /** Return `true` if @p seq has a pending or in-progress database write.
     *
     *  A `true` result means the sequence appears in the map (either
     *  dispatched-but-not-started or actively writing). Callers use this to
     *  avoid re-dispatching a save that is already in flight.
     *
     *  @param seq Ledger sequence number to test.
     */
    bool
    pending(LedgerIndex seq)
    {
        std::scoped_lock const lock(mutex_);
        return map_.contains(seq);
    }

    /** Determine whether the caller should proceed with (or wait for) a save.
     *
     *  This is the entry point for `pendSaveValidated()`. It implements the
     *  full dispatch/steal/wait decision:
     *
     *  - **Not present**: Inserts `(seq, false)` and returns `true` — the
     *    caller owns the work.
     *  - **Present as `false`** (registered, unstarted):
     *    - Asynchronous caller: returns `false` (already dispatched; skip).
     *    - Synchronous caller: returns `true`, stealing the work before any
     *      thread can claim it via `startWork()`.
     *  - **Present as `true`** (write in progress):
     *    - Asynchronous caller: unreachable in practice; the `!isSynchronous`
     *      branch returns `false` before reaching the wait.
     *    - Synchronous caller: blocks on `await_` in a `do/while` loop,
     *      re-checking after each `notify_all()` from `finishWork()`, until
     *      the entry disappears (write complete).
     *
     *  @param seq           Ledger sequence number to check or register.
     *  @param isSynchronous `true` if the caller requires the write to be
     *      complete before returning; `false` if dispatch-once is sufficient.
     *  @return `true` if the caller should perform (or has stolen) the write;
     *      `false` if the work is already dispatched or complete.
     *
     *  @note The blocking synchronous path re-acquires `mutex_` after each
     *      wake-up and loops because `notify_all()` may unblock multiple
     *      waiters; only one will find the entry absent.
     */
    bool
    shouldWork(LedgerIndex seq, bool isSynchronous)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        do
        {
            auto it = map_.find(seq);

            if (it == map_.end())
            {
                map_.emplace(seq, false);
                return true;
            }

            if (!isSynchronous)
            {
                // Already dispatched
                return false;
            }

            if (!it->second)
            {
                // Scheduled, but not dispatched
                return true;
            }

            // Already in progress, just need to wait
            await_.wait(lock);

        } while (true);
    }

    /** Return a point-in-time copy of the pending-saves map.
     *
     *  Used by `LedgerMaster::getValidatedRange()` to trim the reported
     *  min/max validated-ledger range: any sequence present in the snapshot —
     *  regardless of whether its flag is `false` (dispatched) or `true`
     *  (writing) — is excluded from the range to avoid directing peers to
     *  query a partially-written DB row.
     *
     *  The returned map is a value copy taken under `mutex_`; the caller may
     *  iterate it freely without holding any lock.
     *
     *  @return A snapshot of `map_`, where each key is an in-flight ledger
     *      sequence and each value is `false` (unstarted) or `true` (active).
     */
    std::map<LedgerIndex, bool>
    getSnapshot() const
    {
        std::scoped_lock const lock(mutex_);

        return map_;
    }
};

}  // namespace xrpl
