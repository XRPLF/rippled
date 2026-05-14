#pragma once

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

/** Low-level write surface for committing ledger state mutations.
 *
 *  Defines the three-operation contract (`rawErase`, `rawInsert`,
 *  `rawReplace`) plus an XRP-burn hook (`rawDestroyXRP`) that together
 *  represent the minimal interface a backing store must provide to absorb
 *  flushed changes from a sandbox.
 *
 *  `detail::RawStateTable::apply(RawView&)` is the canonical driver:
 *  it iterates its buffered erase/insert/replace actions and dispatches
 *  each through the corresponding method here, so flushing logic is written
 *  once and any concrete target — a finalising `Ledger`, an `OpenView`, or
 *  another sandbox — implements the contract without exposing checkout
 *  semantics.
 *
 *  The "raw" prefix is a semantic contract: these methods perform no
 *  precondition checking, no journaling, and no ownership tracking.
 *  They are the trusted commit surface, not the API that transaction
 *  logic should call directly.
 *
 *  @note The copy constructor is defaulted (subclasses may need to snapshot
 *      state), but copy assignment is deleted to prevent silent cross-type
 *      assignment through the base interface.
 */
class RawView
{
public:
    virtual ~RawView() = default;
    RawView() = default;
    RawView(RawView const&) = default;
    RawView&
    operator=(RawView const&) = delete;

    /** Unconditionally remove an existing state entry.
     *
     *  The full SLE (not just its key) is passed so that implementations
     *  can compute metadata such as changes to owner count or the type of
     *  the deleted object.
     *
     *  @param sle The ledger entry to remove. The key is derived from
     *      the SLE itself; the entry must exist in the backing store.
     */
    virtual void
    rawErase(std::shared_ptr<SLE> const& sle) = 0;

    /** Unconditionally insert a new state entry.
     *
     *  The key is read from the SLE rather than passed separately,
     *  which prevents key/value mismatches at the call site.
     *
     *  @param sle The ledger entry to insert. The key must not already
     *      exist in the backing store.
     */
    virtual void
    rawInsert(std::shared_ptr<SLE> const& sle) = 0;

    /** Unconditionally overwrite an existing state entry.
     *
     *  The key is read from the SLE rather than passed separately,
     *  which prevents key/value mismatches at the call site.
     *
     *  @param sle The replacement ledger entry. The key must already
     *      exist in the backing store.
     */
    virtual void
    rawReplace(std::shared_ptr<SLE> const& sle) = 0;

    /** Permanently remove XRP drops from the ledger supply.
     *
     *  XRPL burns transaction fees rather than redistributing them.
     *  This method is the accounting hook for that burn: separating it
     *  from `rawErase` keeps fee accounting explicit and auditable.
     *
     *  @param fee The quantity of XRP drops to destroy.
     */
    virtual void
    rawDestroyXRP(XRPAmount const& fee) = 0;
};

//------------------------------------------------------------------------------

/** Extends `RawView` with the ability to insert transactions into the
 *  ledger's transaction map.
 *
 *  The split between `RawView` (state-only writes) and `TxsRawView`
 *  (state plus transaction map) is architecturally significant.
 *  `detail::ApplyViewBase` — the sandbox used during transaction
 *  processing — only needs `RawView`: sandboxes accumulate state
 *  mutations but do not independently maintain a transaction map.
 *  `OpenView`, by contrast, inherits both `ReadView` and `TxsRawView`
 *  because it is the accumulation point for an open ledger round and
 *  must track both the growing state diff and the applied-transaction
 *  set.
 */
class TxsRawView : public RawView
{
public:
    /** Insert a serialized transaction into the ledger's transaction map.
     *
     *  @param key      The transaction's map key (typically its hash).
     *  @param txn      Serialized transaction blob; must not be null.
     *  @param metaData Serialized transaction metadata, or null for open
     *      ledgers. Closed ledgers must supply metadata; open ledgers must
     *      pass null because consensus has not yet produced execution
     *      results.
     */
    virtual void
    rawTxInsert(
        ReadView::key_type const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) = 0;
};

}  // namespace xrpl
