/** @file
 *  Runtime enforcement helpers for the XRPL delegate account system.
 *
 *  Transactors call these two functions in sequence during permission
 *  validation: `checkTxPermission` for the broad transaction-type gate,
 *  then `loadGranularPermission` when a more restrictive, field-level
 *  check is needed.  The permission schema and encoding convention live
 *  in `xrpl/protocol/Permissions.h`.
 */
#pragma once

#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

/** Determine whether a delegate relationship grants blanket permission for
 *  a transaction type.
 *
 *  Scans the `sfPermissions` array of the `ltDELEGATE` ledger entry for an
 *  element whose `sfPermissionValue` equals `tx.getTxnType() + 1` — the
 *  transaction-level encoding used on-ledger.  Returns `tesSUCCESS` on the
 *  first match, or `terNO_DELEGATE_PERMISSION` if no match is found.
 *
 *  A null `delegate` pointer is treated as a missing ledger entry and
 *  returns `terNO_DELEGATE_PERMISSION` immediately.
 *
 *  The result is `NotTEC` (no `tec` fee-claim codes) because the two
 *  meaningful outcomes are `tesSUCCESS` and `terNO_DELEGATE_PERMISSION`.
 *  The `ter` (retry) code is intentional: the `ltDELEGATE` object could be
 *  updated in a subsequent ledger, so an identical transaction may succeed
 *  in the future without modification.
 *
 *  @param delegate Immutable `ltDELEGATE` SLE obtained via `view.read()`;
 *      may be null, in which case `terNO_DELEGATE_PERMISSION` is returned.
 *  @param tx The transaction whose type is being checked.
 *  @return `tesSUCCESS` if the delegate holds a transaction-level permission
 *      for `tx`'s type; `terNO_DELEGATE_PERMISSION` otherwise.
 *  @note Callers should resolve the SLE via `keylet::delegate(account,
 *      delegate)` and pass it directly.  If the SLE is absent from the
 *      ledger, `view.read()` returns null and the guard here handles it.
 *  @see loadGranularPermission — for fine-grained per-flag enforcement when
 *      this function returns `terNO_DELEGATE_PERMISSION`.
 */
NotTEC
checkTxPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx);

/** Populate a set with all granular sub-operation permissions the delegate
 *  holds for a given transaction type.
 *
 *  Walks the `sfPermissions` array of the `ltDELEGATE` ledger entry.  For
 *  each element, it casts the `sfPermissionValue` to `GranularPermissionType`
 *  and asks `Permission::getInstance().getGranularTxType()` whether that
 *  granular type belongs to `type`.  Matching values are inserted into
 *  `granularPermissions`.
 *
 *  A null `delegate` pointer is a silent no-op; the output set is left
 *  unchanged.
 *
 *  The set is caller-owned and passed by reference so transactors can declare
 *  it on the stack, avoiding heap allocation.  Callers may also accumulate
 *  results from multiple calls if needed.
 *
 *  @param delegate Immutable `ltDELEGATE` SLE; may be null (no-op).
 *  @param type The transaction type whose granular permissions should be
 *      collected (e.g., `ttTRUST_SET`, `ttPAYMENT`).
 *  @param granularPermissions Output set populated with every
 *      `GranularPermissionType` the delegate holds that maps to `type`.
 *  @note This function is the second stage of a two-step check.  Call
 *      `checkTxPermission` first; only invoke this when that returns
 *      `terNO_DELEGATE_PERMISSION` and the transaction type supports
 *      granular flags.  Calling it unconditionally wastes a full scan of
 *      the permissions array on the common case.
 *  @see checkTxPermission — for the broad transaction-type gate.
 */
void
loadGranularPermission(
    std::shared_ptr<SLE const> const& delegate,
    TxType const& type,
    std::unordered_set<GranularPermissionType>& granularPermissions);

}  // namespace xrpl
