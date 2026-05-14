/** @file
 *  Asset eligibility interface for the XRPL payment path-finding subsystem.
 *
 *  Declares `accountSourceAssets` and `accountDestAssets`, which answer the
 *  question "what can this account send / receive?" without exposing `AssetCache`
 *  internals or the raw trust-line / MPT SLE representations to callers.
 *  Both functions are consumed exclusively by `PathRequest` to scope the
 *  `Pathfinder` graph traversal to assets that are actually usable.
 */

#pragma once

#include <xrpld/rpc/detail/AssetCache.h>

#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Enumerate every asset that @p account is capable of receiving.
 *
 *  Scans the account's outgoing trust lines and MPT holdings from @p cache
 *  and returns the subset of assets for which there is remaining receive
 *  capacity.  Called by `PathRequest` to populate the
 *  `destination_currencies` field in both the streaming `path_find` and the
 *  legacy `ripple_path_find` responses.
 *
 *  An IOU currency is included when `balance < ownLimit`, i.e. the account's
 *  self-imposed trust limit has not been reached.  An MPT issuance is included
 *  when the account holds a *zero* balance (fresh, not yet funded) and the
 *  issuer's supply ceiling has not been reached (`!isMaxedOut()`).
 *
 *  XRP is always a valid destination even if the account does not yet exist,
 *  because account creation requires an XRP payment above the base reserve.
 *
 *  The sentinel `badCurrency()` is removed from the result as a defensive
 *  measure against malformed trust-line entries.
 *
 *  @note The MPT eligibility condition (zero balance) is the *inverse* of the
 *      source check in `accountSourceAssets` (non-zero balance).  A freshly
 *      authorized or recently emptied MPT holder has zero balance and is the
 *      correct receive target; an MPT holder with a positive balance is the
 *      correct send source.
 *
 *  @note The caller is responsible for honoring `lsfDisallowXRP`: pass
 *      `!disallowXRP` as @p includeXRP when the flag is set on the destination
 *      account so that XRP is suppressed at the RPC layer rather than here.
 *
 *  @param account    The destination account to evaluate.
 *  @param cache      Ledger-snapshot cache shared within a path-finding
 *      session; amortises repeated SLE reads across multiple calls.
 *  @param includeXRP When true, XRP is unconditionally added to the result.
 *  @return A deduplicated set of `PathAsset` values (each a `Currency` or
 *      `MPTID`) that the account can accept.
 */
hash_set<PathAsset>
accountDestAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& cache,
    bool includeXRP);

/** Enumerate every asset that @p account is capable of sending.
 *
 *  Scans the account's outgoing trust lines and MPT holdings from @p cache
 *  and returns the subset of assets that can be pushed outward.  Called by
 *  `PathRequest` when no explicit `SendMax` asset has been provided, to seed
 *  the `Pathfinder` with all possible source currencies before graph traversal.
 *
 *  An IOU currency is included when either:
 *  - the account's balance is positive (it holds issued currency to push), or
 *  - the account is a net borrower with room left under the peer's credit
 *    limit (`(-balance) < peerLimit`).
 *
 *  An MPT issuance is included when the holder's balance is non-zero
 *  (`!isZeroBalance()`) and the issuer's supply ceiling has not been reached
 *  (`!isMaxedOut()`).
 *
 *  The sentinel `badCurrency()` is removed from the result as a defensive
 *  measure against malformed trust-line entries.
 *
 *  @note No reserve check is performed before inserting XRP; the caller is
 *      responsible for gating on account balance if that distinction matters.
 *
 *  @note This function returns *all* spendable assets without an upper-bound.
 *      The caller (`PathRequest`) caps auto-detected source currencies at
 *      `RPC::Tuning::max_auto_src_cur` after this function returns.
 *
 *  @param account    The source account to evaluate.
 *  @param lrLedger   Ledger-snapshot `AssetCache` shared within a path-finding
 *      session; amortises repeated SLE reads across multiple calls.
 *      (The parameter name is a historical artifact; the type is `AssetCache`,
 *      not a raw ledger view.)
 *  @param includeXRP When true, XRP is unconditionally added to the result.
 *  @return A deduplicated set of `PathAsset` values (each a `Currency` or
 *      `MPTID`) that the account can send.
 */
hash_set<PathAsset>
accountSourceAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrLedger,
    bool includeXRP);

}  // namespace xrpl
