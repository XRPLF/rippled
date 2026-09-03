#pragma once

#include <test/jtx/Env.h>

#include <chrono>
#include <optional>

namespace xrpl::test::jtx {

/**
 * How long the waits here allow before giving up. Long enough that a loaded CI
 * machine does not trip it, short enough that something genuinely stuck fails
 * the test rather than hanging the unit test job until CI's global timeout.
 */
inline constexpr std::chrono::seconds kRendezvousTimeout{60};

/**
 * Wait until the SHAMapStore has finished processing the ledger that the
 * preceding env.close() produced. Returns false if either wait times out.
 *
 * env.close() returns as soon as the ledger_accept RPC returns, but the
 * validated ledger path -- LedgerMaster::setValidLedger() ->
 * SHAMapStore::onLedgerClosed() -- runs on a job queue thread. Without draining
 * the job queue first, the store may not have been handed the ledger at all, in
 * which case rendezvous() observes working_ == false and returns immediately,
 * before any work has been done.
 */
[[nodiscard]] bool
syncStore(Env& env, std::chrono::milliseconds timeout = kRendezvousTimeout);

/**
 * Bring the SHAMapStore to the point where it has been handed a validated
 * ledger and initialized lastRotated, and report how many extra ledgers had to
 * be closed to get it there (normally none). Returns std::nullopt if syncStore()
 * itself failed.
 *
 * syncStore() alone does not guarantee that, because SHAMapStoreImp::run()'s
 * loop does not use the notification and the working_ flag safely:
 *
 *   * onLedgerClosed() notifies cond_ whether or not run()'s thread is parked
 *     on it, and run() waits on cond_ without a predicate, so a notification
 *     that lands while the thread is still starting up -- before it first
 *     reaches that wait -- is lost.
 *   * run() clears working_ at the top of its loop without checking whether
 *     newLedger_ is still set, so rendezvous() can report the store idle with a
 *     validated ledger queued.
 *
 * Either way the store ends up parked with work pending, and only another
 * notification gets it moving again. In a standalone test nothing else closes
 * ledgers, so that has to come from here: this closes a ledger rather than
 * polling getLastRotated(), because polling would just time out.
 * onLedgerClosed() keeps only the most recent ledger in newLedger_, so the
 * ledger the store picks up -- and therefore lastRotated -- is a timing detail,
 * which is why callers should derive their expectations from the value they
 * observe instead of assuming one.
 *
 * run() is deliberately left as it is. In production the only effect is
 * latency: the trigger is validatedSeq >= lastRotated + deleteInterval, so a
 * lost notification delays rotation to the next validated ledger and nothing is
 * skipped or accumulated -- starting at 513 instead of 512 does not matter. Two
 * consequences do follow from leaving it in place, and both hold today: nothing
 * in production decides anything from working_ or rendezvous() (rendezvous()
 * has no production callers at all), and a node whose ledgers only advance on
 * demand -- standalone, driven by ledger_accept -- can sit on a queued ledger
 * until something closes the next one, which is exactly the situation this
 * helper is working around.
 *
 * So this helper is permanent rather than a stopgap. Working around the race
 * must not make it invisible, so every extra close is logged. That keeps how
 * often it is actually hit observable in the unit test output -- which is the
 * only signal left once the testcases that use it stop flaking on it.
 */
[[nodiscard]] std::optional<int>
initializeStore(Env& env, int maxExtraCloses = 3);

}  // namespace xrpl::test::jtx
