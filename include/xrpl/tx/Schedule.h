#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/AccessSet.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace xrpl {

class ServiceRegistry;

/** A maximal set of transactions that conflict (transitively) with each other.

    The transactions are held in canonical (input) order, which is significant:
    within a group they must be applied serially in this order. Two distinct
    groups are independent by construction — their declared access sets are
    disjoint — so groups may be applied concurrently, and the order in which
    groups are applied does not affect the resulting state.
*/
struct ConflictGroup
{
    std::vector<std::shared_ptr<STTx const>> txns;
};

/** The partitioning of a canonically-ordered transaction set for parallel
    application.

    Either the set was partitioned into independent `groups` (the parallel
    case), or scheduling fell back to a single serial ordering in `serial`
    (the conservative case — see `scheduleApply`). Exactly one of the two is
    populated; `fullySerial` says which.
*/
struct Schedule
{
    std::vector<ConflictGroup> groups;
    std::vector<std::shared_ptr<STTx const>> serial;
    bool fullySerial = false;

    /** Total transactions scheduled, across groups or the serial list. */
    [[nodiscard]] std::size_t
    size() const
    {
        if (fullySerial)
            return serial.size();
        std::size_t n = 0;
        for (auto const& g : groups)
            n += g.txns.size();
        return n;
    }
};

/** Partition a canonically-ordered transaction set into independent groups by
    static access-set conflict, for parallel application.

    For each transaction, `accessSetOf` is consulted against `base` (the closed
    -ledger snapshot the round applies to). Transactions are unioned into the
    same group iff their access sets conflict — i.e. they share any declared
    ledger entry, or (implicitly, via the access set) act on the same account.

    If ANY transaction declares `touchesGlobal` (a pseudo-transaction such as
    SetFee/EnableAmendment/UNLModify, or any not-yet-migrated transactor), the
    schedule falls back to fully serial in canonical order. This is the
    conservative flag-ledger handling: correctness over throughput, at a cost of
    ~1/256 of ledgers. A later version may apply the global transactions first
    and parallelize the remainder.

    Deterministic: identical input yields an identical schedule (groups are
    ordered by their lowest canonical index, transactions within a group keep
    canonical order). Applies nothing and reads only `base`.
*/
Schedule
scheduleApply(std::vector<std::shared_ptr<STTx const>> const& txns, ReadView const& base);

/** Outcome of applyScheduled. */
struct ScheduledApplyResult
{
    std::size_t groupCount = 0;  // independent groups (1 if fully serial)
    std::size_t applied = 0;     // transactions that applied successfully
    bool fullySerial = false;
};

/** Apply a canonically-ordered transaction set via its schedule.

    Schedules `txns` (see `scheduleApply`), then applies each independent group
    in its own `OpenView` layered over the immutable `closed` snapshot, and
    merges the per-group write-sets into `to`. Because distinct groups have
    disjoint access sets, their write-sets are disjoint and the merge is
    order-independent — so the resulting state in `to` is byte-identical to a
    serial canonical apply. (That equivalence is what the differential test
    asserts, and it is the correctness contract a parallel executor relies on.)

    `workers` controls concurrency of the per-group apply phase:
      - `workers <= 1`: groups applied sequentially (deterministic baseline).
      - `workers > 1`: up to `workers` groups apply concurrently, each in its own
        view over the immutable `closed` snapshot; the write-sets are then merged
        into `to` sequentially in deterministic group order.
    The result is identical for any `workers` value (the merge is order-
    independent because groups are disjoint, and is performed in a fixed order).

    NOTE on the threaded path: it is correct by construction here, but it is NOT
    certified for production consensus use. A non-deterministic apply forks the
    network, so before the threaded path may be trusted it needs ThreadSanitizer
    clean runs, adversarial scheduling (Antithesis), and a mainnet-history replay
    differential — none of which a unit test establishes. The concurrent reads
    of `closed` and the shared `registry` are the surfaces that must be cleared.

    @param registry service registry used by the apply pipeline.
    @param closed   the immutable closed-ledger snapshot to read and schedule against.
    @param to       the target ledger receiving the merged writes.
    @param txns     transactions in canonical order.
    @param j        journal.
    @param workers  max concurrent group-apply threads (default 1 = sequential).
*/
ScheduledApplyResult
applyScheduled(
    ServiceRegistry& registry,
    ReadView const& closed,
    TxsRawView& to,
    std::vector<std::shared_ptr<STTx const>> const& txns,
    beast::Journal j,
    unsigned workers = 1);

}  // namespace xrpl
