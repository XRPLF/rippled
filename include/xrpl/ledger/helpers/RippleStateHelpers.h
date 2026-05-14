/** @file
 *  IOU trustline (RippleState) operations for the XRP Ledger.
 *
 *  Declares every ledger operation that reads from or writes to a
 *  `RippleState` (trustline) SLE: credit-limit and balance queries,
 *  freeze checks, trustline lifecycle, IOU issuance/redemption,
 *  authorization and rippling enforcement, zero-balance holding
 *  management, and AMM-specific cleanup.
 *
 *  This file is the IOU-specific leaf of the token helper layer.
 *  Asset-agnostic callers should go through the dispatchers in
 *  `TokenHelpers.h`, which branch on `Issue` vs `MPTIssue` and
 *  delegate here for the IOU path.
 *
 *  @note The trustline orientation invariant is pervasive here:
 *      `sfLowLimit` always belongs to the account whose `AccountID`
 *      compares less; `sfHighLimit` to the other.  Every function
 *      applies this flip internally — callers supply `(account, issuer)`
 *      and receive results in account-centric terms.
 */
#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

// --- RippleState (Trustline) helpers ---

namespace xrpl {

// --- Credit queries ---

/** Read the maximum IOU balance that @p account has authorised @p issuer to
 *  carry on their behalf.
 *
 *  Reads `sfLowLimit` or `sfHighLimit` from the trustline depending on
 *  which side `account` occupies (low if `account < issuer`).  The issuer
 *  field of the returned amount is rewritten to `account` so the result is
 *  safe to consume without knowing the binary-ordering of the two accounts.
 *  Returns a zero-valued `STAmount` (with the correct issue) if no trustline
 *  exists.
 *
 *  @param view     Read-only ledger view to query.
 *  @param account  The account whose credit limit is requested.
 *  @param issuer   The IOU issuer.
 *  @param currency The currency of the trustline.
 *  @return The credit limit expressed from @p account's perspective, or zero
 *      if no trustline exists.
 */
/** @{ */
STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency);

/** Convenience wrapper returning the credit limit as `IOUAmount`.
 *
 *  @param v   Read-only ledger view to query.
 *  @param acc The account whose credit limit is requested.
 *  @param iss The IOU issuer.
 *  @param cur The currency of the trustline.
 *  @return The credit limit as `IOUAmount`, or zero if no trustline exists.
 *  @see creditLimit
 */
IOUAmount
creditLimit2(ReadView const& v, AccountID const& acc, AccountID const& iss, Currency const& cur);
/** @} */

/** Read the IOU balance that @p account currently holds.
 *
 *  `sfBalance` is stored in "low-account-sends-to-high-account" orientation.
 *  When `account` is the high side the stored value is negated before being
 *  returned, so callers always receive a balance expressed as "how much of
 *  this currency does @p account hold", regardless of which slot they occupy
 *  on the trustline.  Returns zero (with the correct issue) if no trustline
 *  exists.
 *
 *  @param view     Read-only ledger view to query.
 *  @param account  The account whose balance is requested.
 *  @param issuer   The IOU issuer.
 *  @param currency The currency of the trustline.
 *  @return The balance expressed from @p account's perspective, or zero if
 *      no trustline exists.
 */
/** @{ */
STAmount
creditBalance(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency);
/** @} */

// --- Freeze checks (IOU-specific) ---

/** Check whether @p issuer has individually frozen @p account's trustline.
 *
 *  Inspects only the issuer's side flag (`lsfLowFreeze`/`lsfHighFreeze`) on
 *  the trustline.  Does **not** check the issuer's global freeze flag — use
 *  `isFrozen` for that combined check.  Always returns `false` for XRP.
 *
 *  @param view     Read-only ledger view.
 *  @param account  The account to test.
 *  @param currency The IOU currency.
 *  @param issuer   The IOU issuer.
 *  @return `true` if the issuer has set a line-level freeze on this account.
 */
[[nodiscard]] bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

/** Convenience overload accepting an `Issue`.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param issue   The IOU issue (currency + issuer).
 *  @return `true` if the issuer has set a line-level freeze on this account.
 *  @see isIndividualFrozen(ReadView const&, AccountID const&, Currency const&,
 *      AccountID const&)
 */
[[nodiscard]] inline bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isIndividualFrozen(view, account, issue.currency, issue.account);
}

/** Check whether @p account is frozen for @p currency issued by @p issuer.
 *
 *  Returns `true` if either the issuer's `AccountRoot` has `lsfGlobalFreeze`
 *  set, or the issuer has frozen this specific trustline (`lsfLowFreeze` /
 *  `lsfHighFreeze`).  Always returns `false` for XRP or when
 *  `account == issuer`.  This is the check used by payment paths.
 *
 *  @param view     Read-only ledger view.
 *  @param account  The account to test.
 *  @param currency The IOU currency.
 *  @param issuer   The IOU issuer.
 *  @return `true` if the account cannot move this IOU due to any freeze.
 */
[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

/** Convenience overload accepting an `Issue`.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param issue   The IOU issue (currency + issuer).
 *  @return `true` if the account cannot move this IOU due to any freeze.
 *  @see isFrozen(ReadView const&, AccountID const&, Currency const&,
 *      AccountID const&)
 */
[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isFrozen(view, account, issue.currency, issue.account);
}

/** Overload accepting a depth parameter for interface uniformity with MPT.
 *
 *  IOUs do not have vault-level recursion, so the `depth` argument is
 *  unconditionally ignored.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param issue   The IOU issue (currency + issuer).
 *  @return `true` if the account cannot move this IOU due to any freeze.
 */
[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Issue const& issue, int /*depth*/)
{
    return isFrozen(view, account, issue);
}

/** Check whether @p account is deep-frozen for @p currency issued by
 *  @p issuer.
 *
 *  Deep-freeze (`lsfHighDeepFreeze` / `lsfLowDeepFreeze`) is a stricter
 *  condition than ordinary freeze: it prevents both sending *and* receiving
 *  the currency.  Always returns `false` for XRP, and always returns `false`
 *  when `issuer == account` (an issuer cannot deep-freeze their own balance
 *  with themselves).
 *
 *  @param view     Read-only ledger view.
 *  @param account  The account to test.
 *  @param currency The IOU currency.
 *  @param issuer   The IOU issuer.
 *  @return `true` if the deep-freeze flag is set on either side of the line.
 */
[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

/** Convenience overload accepting an `Issue`, with an optional depth parameter
 *  for interface uniformity with the MPT equivalent.
 *
 *  The `depth` argument is unconditionally ignored for IOUs.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param issue   The IOU issue (currency + issuer).
 *  @return `true` if the deep-freeze flag is set on either side of the line.
 *  @see isDeepFrozen(ReadView const&, AccountID const&, Currency const&,
 *      AccountID const&)
 */
[[nodiscard]] inline bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    int = 0 /*ignored*/)
{
    return isDeepFrozen(view, account, issue.currency, issue.account);
}

/** Convert a deep-freeze check into a `TER` result.
 *
 *  Convenience wrapper for transactor preflight code that returns
 *  `tecFROZEN` if the account is deep-frozen and `tesSUCCESS` otherwise.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param issue   The IOU issue (currency + issuer).
 *  @return `tecFROZEN` if deep-frozen, `tesSUCCESS` otherwise.
 */
[[nodiscard]] inline TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isDeepFrozen(view, account, issue) ? (TER)tecFROZEN : (TER)tesSUCCESS;
}

// --- Trust line lifecycle ---

/** Create a new `RippleState` (trustline) SLE and insert it into both owner
 *  directories.
 *
 *  This is the lowest-level entry point for trustline creation.  It is called
 *  directly by `TrustSet` transactors and indirectly by `issueIOU` when the
 *  destination has no existing line.
 *
 *  The function writes all trustline fields — limits, quality in/out, balance,
 *  and flag bits — using side-aware field selectors (`sfLowLimit`/`sfHighLimit`
 *  etc.) derived from `bSrcHigh`.  The peer account's `lsfNoRipple` flag is
 *  initialised from the peer's `lsfDefaultRipple` setting (absent means
 *  noRipple is on by default).
 *
 *  @param view           Mutable ledger view.
 *  @param bSrcHigh       `true` if `uSrcAccountID` occupies the "high" slot
 *      (i.e., `uSrcAccountID > uDstAccountID`).
 *  @param uSrcAccountID  The account whose limit and flags are being
 *      configured.
 *  @param uDstAccountID  The peer account on the other side of the line.
 *  @param uIndex         Pre-calculated keylet key for the new SLE.
 *  @param sleAccount     The `AccountRoot` SLE for the account being set
 *      (used to adjust owner count); must not be null.
 *  @param bAuth          If `true`, set the authorization flag on the source
 *      side of the line.
 *  @param bNoRipple      If `true`, set `lsfNoRipple` on the source side.
 *  @param bFreeze        If `true`, set the freeze flag on the source side.
 *  @param bDeepFreeze    If `true`, set the deep-freeze flag on the source
 *      side.
 *  @param saBalance      Initial balance from the source account's
 *      perspective; the issuer field must be `noAccount()`.
 *  @param saLimit        Credit limit for the source account; the issuer
 *      field must be `uSrcAccountID`.
 *  @param uQualityIn     Quality-in override (0 = default/no override).
 *  @param uQualityOut    Quality-out override (0 = default/no override).
 *  @param j              Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, `tecDIR_FULL` if either owner directory
 *      is at capacity, `tecNO_TARGET` if the peer account does not exist,
 *      or `tefINTERNAL` if `sleAccount` is null or has a mismatched ID.
 */
[[nodiscard]] TER
trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // --> ripple state entry
    SLE::ref sleAccount,        // --> the account being set.
    bool const bAuth,           // --> authorize account.
    bool const bNoRipple,       // --> others cannot ripple through
    bool const bFreeze,         // --> funds cannot leave
    bool bDeepFreeze,           // --> can neither receive nor send funds
    STAmount const& saBalance,  // --> balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // --> limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    beast::Journal j);

/** Delete a `RippleState` (trustline) SLE and remove its directory backlinks.
 *
 *  Removes the SLE from both the low and high owner directories using the
 *  `sfLowNode`/`sfHighNode` deletion hints stored inside the SLE itself,
 *  then erases the SLE from the view.
 *
 *  @param view              Mutable ledger view.
 *  @param sleRippleState    The trustline SLE to delete; must be obtained
 *      from `view.peek()`.
 *  @param uLowAccountID     The account occupying the low slot.
 *  @param uHighAccountID    The account occupying the high slot.
 *  @param j                 Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, `tefBAD_LEDGER` if either directory
 *      removal fails (indicating ledger corruption).
 */
[[nodiscard]] TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j);

// --- IOU issuance/redemption ---

/** Issue IOUs from @p issue.account to @p account, adjusting the trustline
 *  balance.
 *
 *  Debits the issuer's side of the trustline and credits the receiver.  After
 *  adjusting the balance, calls the internal `updateTrustLine` helper: if the
 *  sender's balance crosses zero and seven specific cleanup conditions are met
 *  (zero limit, no freeze, etc.), the sender's reserve is released and the
 *  line may be deleted via `trustDelete`.
 *
 *  If no trustline exists for the receiver, one is created via `trustCreate`,
 *  inheriting the receiver's `lsfDefaultRipple` setting for the initial
 *  `lsfNoRipple` state.  Always invokes `view.creditHookIOU()` after mutating
 *  the balance.
 *
 *  @param view    Mutable ledger view.
 *  @param account The account receiving the IOUs (must not be the issuer).
 *  @param amount  The amount to issue; its `Issue` must match @p issue.
 *  @param issue   Identifies the currency and issuer.
 *  @param j       Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, or a `tef`/`tec` code propagated from
 *      `trustCreate` or `trustDelete` if an error occurs.
 */
[[nodiscard]] TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

/** Redeem IOUs held by @p account back toward the issuer, adjusting the
 *  trustline balance.
 *
 *  The mirror image of `issueIOU`: credits the issuer and debits the holder.
 *  After adjusting the balance, calls `updateTrustLine` for the same
 *  automatic cleanup logic.  Always invokes `view.creditHookIOU()` after
 *  mutating the balance.
 *
 *  Unlike `issueIOU`, a missing trustline is treated as a fatal internal
 *  error (`tefINTERNAL`) because it is impossible to redeem a balance on a
 *  line that does not exist.
 *
 *  @param view    Mutable ledger view.
 *  @param account The account redeeming IOUs (must not be the issuer).
 *  @param amount  The amount to redeem; its `Issue` must match @p issue.
 *  @param issue   Identifies the currency and issuer.
 *  @param j       Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, `tefINTERNAL` if no trustline exists,
 *      or a `tef`/`tec` code from `trustDelete` if cleanup triggers an error.
 */
[[nodiscard]] TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

// --- Authorization and transfer checks (IOU-specific) ---

/** Check whether @p account is authorized to hold the IOU described by
 *  @p issue.
 *
 *  Behaviour depends on @p authType:
 *  - **`StrongAuth`**: Returns `tecNO_LINE` immediately if no trustline
 *    exists.  If the issuer has `lsfRequireAuth` and the line exists but is
 *    not authorized, returns `tecNO_AUTH`.
 *  - **`WeakAuth`** / **`Legacy`** (equivalent for IOUs): Returns
 *    `tecNO_AUTH` if `lsfRequireAuth` is set, the line exists, but is not
 *    authorized.  Returns `tecNO_LINE` if auth is required and no line
 *    exists.  If `lsfRequireAuth` is not set, returns `tesSUCCESS` even when
 *    no line exists — appropriate for payment path-finding where a line may
 *    be created on the fly.
 *
 *  Always returns `tesSUCCESS` for XRP or when `account == issue.account`.
 *
 *  @param view     Read-only ledger view.
 *  @param issue    The IOU to check authorization for.
 *  @param account  The account to check.
 *  @param authType Authorization strictness; defaults to `AuthType::Legacy`
 *      (equivalent to `WeakAuth` for IOUs).
 *  @return `tesSUCCESS`, `tecNO_AUTH`, or `tecNO_LINE`.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    Issue const& issue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy);

/** Check whether an IOU can be transferred between @p from and @p to via the
 *  issuer's trustlines.
 *
 *  Returns `tesSUCCESS` unconditionally when either endpoint is the issuer,
 *  or when the IOU is native (XRP).  For third-party transfers, returns
 *  `terNO_RIPPLE` only when both the `from` and the `to` trustlines have
 *  `lsfNoRipple` set on the issuer's side, blocking rippling through.  If a
 *  trustline does not exist for a given account, the issuer's
 *  `lsfDefaultRipple` flag is consulted as a fallback preference.
 *
 *  @param view   Read-only ledger view.
 *  @param issue  The IOU (identifies the issuer and currency).
 *  @param from   The sending account.
 *  @param to     The receiving account.
 *  @return `tesSUCCESS` if the transfer is permitted, `terNO_RIPPLE` if
 *      rippling is disabled on both sides.
 */
[[nodiscard]] TER
canTransfer(ReadView const& view, Issue const& issue, AccountID const& from, AccountID const& to);

// --- Empty holding operations (IOU-specific) ---

/** Create a zero-balance trustline for @p accountID, reserving the destination
 *  slot before any funds arrive.
 *
 *  Used by transactors (e.g., DEX limit orders) that need to guarantee a
 *  destination line exists before settlement.  Checks that @p accountID can
 *  cover the increased owner-count reserve before calling `trustCreate`.
 *
 *  Returns `tesSUCCESS` immediately for XRP or when `accountID` is the
 *  issuer.  Returns `tecDUPLICATE` if the trustline already exists.
 *
 *  @note Any transactor that calls this function in `doApply` **must** call
 *      `canAddHolding()` (declared in `TokenHelpers.h`) in `preflight` with
 *      the same view and asset to validate the reserve precondition.
 *
 *  @param view         Mutable ledger view.
 *  @param accountID    The account that will hold the IOU.
 *  @param priorBalance The account's XRP balance before the current
 *      transaction, used to test reserve sufficiency.
 *  @param issue        The IOU to create a holding for.
 *  @param journal      Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success; `tecFROZEN` if the issuer is globally
 *      frozen; `tecNO_LINE_INSUF_RESERVE` if the account cannot afford the
 *      reserve; `tecDUPLICATE` if the line already exists; or a `tec`/`tef`
 *      code from `trustCreate`.
 */
[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Issue const& issue,
    beast::Journal journal);

/** Delete a zero-balance trustline previously created by `addEmptyHolding`.
 *
 *  Validates that the balance is actually zero before deletion.  Adjusts
 *  owner counts for both the low and high sides if their reserve flags are
 *  set, then calls `trustDelete`.
 *
 *  @param view       Mutable ledger view.
 *  @param accountID  The account whose holding line should be removed.
 *  @param issue      The IOU identifying the trustline to remove.
 *  @param journal    Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success; `tecHAS_OBLIGATIONS` if the balance is
 *      non-zero; `tecOBJECT_NOT_FOUND` if no line exists (and the account
 *      is not the issuer); or a `tef`/`tec` code from `trustDelete`.
 */
[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Issue const& issue,
    beast::Journal journal);

/** Delete a trustline owned by an AMM pool account during AMM withdrawal.
 *
 *  Validates that:
 *  - @p sleState is a non-null `ltRIPPLE_STATE` SLE.
 *  - Exactly one of the two trustline endpoints is an AMM account
 *    (identified by the presence of `sfAMMID` in the `AccountRoot`).
 *  - If @p ammAccountID is provided, it matches one of the endpoints.
 *
 *  On success, calls `trustDelete` and decrements the owner count of the
 *  non-AMM side.
 *
 *  @param view         Mutable ledger view.
 *  @param sleState     The `ltRIPPLE_STATE` SLE to delete; must be obtained
 *      from `view.peek()`.
 *  @param ammAccountID If provided, the expected AMM account ID; the
 *      function returns `terNO_AMM` if neither endpoint matches.
 *  @param j            Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success; `tecINTERNAL` if the SLE is null, has
 *      the wrong type, if both sides are AMM, or if the reserve flag is
 *      unexpectedly absent; `terNO_AMM` if neither endpoint is an AMM or
 *      the optional ID does not match; or a `tef` code from `trustDelete`.
 */
[[nodiscard]] TER
deleteAMMTrustLine(
    ApplyView& view,
    std::shared_ptr<SLE> sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j);

/** Delete an AMM account's `MPToken` SLE during AMM withdrawal.
 *
 *  Removes the `MPToken` SLE from @p ammAccountID's owner directory and
 *  erases it from the view.  The caller is responsible for any balance
 *  assertions before invoking this function.
 *
 *  @param view         Mutable ledger view.
 *  @param sleMPT       The `MPToken` SLE to delete; must be obtained from
 *      `view.peek()`.
 *  @param ammAccountID The AMM account that owns the `MPToken`.
 *  @param j            Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, `tefBAD_LEDGER` if the directory removal
 *      fails (indicating ledger corruption).
 */
[[nodiscard]] TER
deleteAMMMPToken(
    ApplyView& view,
    std::shared_ptr<SLE> sleMPT,
    AccountID const& ammAccountID,
    beast::Journal j);

}  // namespace xrpl
