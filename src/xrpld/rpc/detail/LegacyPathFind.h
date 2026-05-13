#pragma once

#include <atomic>

namespace xrpl {

class Application;

namespace RPC {

/** RAII admission-control guard for the synchronous `ripple_path_find` RPC.
 *
 *  Path-finding is orders of magnitude more expensive than a typical RPC call.
 *  This guard enforces a tiered throttle so that at most
 *  `Tuning::kMAX_PATHFINDS_IN_PROGRESS` (2) non-admin path-find operations run
 *  concurrently. Construction either acquires a slot (setting `isOk()` true) or
 *  fails admission (leaving `isOk()` false) with no side effects. The acquired
 *  slot is released automatically on destruction.
 *
 *  The guard is used in two call sites: the `ripple_path_find` handler
 *  (`RipplePathFind.cpp`, when the caller names a specific ledger) and the
 *  auto-bridging path inside `TransactionSign.cpp`. Both sites follow the same
 *  pattern — construct on the stack, call `isOk()`, return `rpcTOO_BUSY` on
 *  failure, then proceed with path-finding for the lifetime of the guard.
 *
 *  @note Admin callers (derived from `isUnlimited(context.role)`) bypass all
 *      throttle checks and always acquire a slot. Non-admin requests are also
 *      rejected early when the job queue exceeds
 *      `Tuning::kMAX_PATHFIND_JOB_COUNT` (50) or local fee load is elevated.
 *      The concurrent-pathfind ceiling is enforced via a process-wide
 *      `static std::atomic<int>` shared across both call sites, so the cap of
 *      2 simultaneous legacy path-finds is a global invariant, not per-handler.
 *
 *  @see `Tuning::kMAX_PATHFINDS_IN_PROGRESS`, `Tuning::kMAX_PATHFIND_JOB_COUNT`
 */
class LegacyPathFind
{
public:
    /** Attempt to acquire a path-find slot.
     *
     *  For non-admin callers, admission is refused if the job queue is
     *  saturated, local fee load is elevated, or the concurrent path-find
     *  ceiling has been reached. The ceiling is enforced via a lock-free CAS
     *  loop on `inProgress`; a failed CAS due to a racing increment causes a
     *  retry rather than an over-count. Call `isOk()` after construction to
     *  determine whether the slot was granted.
     *
     *  @param isAdmin  True if the caller holds an unlimited/admin role;
     *      bypasses all throttle checks when set.
     *  @param app      The running Application, used to query job-queue depth
     *      and local fee load.
     */
    LegacyPathFind(bool isAdmin, Application& app);

    /** Release the acquired slot, if any.
     *
     *  Decrements the global `inProgress` counter only when `isOk()` is true,
     *  i.e. only when construction actually acquired a slot. Safe to call on a
     *  failed-admission instance.
     */
    ~LegacyPathFind();

    /** Returns true if a path-find slot was successfully acquired.
     *
     *  Must be checked immediately after construction. If false, the handler
     *  should return `rpcError(RpcTooBusy)` without performing any path-finding.
     */
    [[nodiscard]] bool
    isOk() const
    {
        return isOk_;
    }

private:
    /** Global count of path-find operations currently in progress. */
    static std::atomic<int> inProgress;

    /** True only when this instance incremented `inProgress` in the constructor. */
    bool isOk_{false};
};

}  // namespace RPC
}  // namespace xrpl
