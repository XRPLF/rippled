#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <memory>

namespace xrpl {

/** Remove an offer and its directory back-references from the ledger.
 *
 *  Performs the full teardown sequence atomically within the transaction
 *  buffer: removes the offer from the owner's directory, removes it from
 *  the order-book quality directory, decrements the owner's reserve count,
 *  and erases the SLE. For hybrid offers (flagged `lsfHybrid`) that
 *  participate in one or more Permissioned DEX domains, each entry in
 *  `sfAdditionalBooks` is also removed from its domain-specific book
 *  directory before the owner-count adjustment and erasure.
 *
 *  If `sle` is null the function returns `tesSUCCESS` immediately,
 *  allowing callers to pass the result of a failed `peek()` without
 *  a pre-check (defensive against double-delete within one batch).
 *
 *  @pre The offer SLE must exist in the ledger and both its
 *      `sfOwnerNode` and `sfBookNode` back-references must be valid.
 *  @pre The caller must have already verified that the submitting
 *      account is authorized to delete this offer; this function
 *      performs no ownership or permission check.
 *
 *  @param view The `ApplyView` transaction buffer to modify.
 *  @param sle  The offer SLE to delete. May be null (treated as no-op).
 *  @param j    Journal for diagnostic logging.
 *
 *  @return `tesSUCCESS` on success, or `tefBAD_LEDGER` if a directory
 *      back-reference is missing (invariant violation; should not occur
 *      in a well-formed ledger).
 *
 *  @note `[[nodiscard]]` is intentionally absent: `BookTip` and payment
 *      path callers do not always inspect the return value, and enforcing
 *      the attribute would have broken compilation across the engine.
 */
// [[nodiscard]] // nodiscard commented out so Flow, BookTip and others compile.
TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);

}  // namespace xrpl
