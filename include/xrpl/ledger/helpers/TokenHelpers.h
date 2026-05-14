/** @file
 *  Asset-agnostic dispatcher layer for all token operations on the XRP Ledger.
 *
 *  This header is the unified entry point for token operations that must work
 *  across XRPL's three asset classes: XRP, IOU (trust-line-based), and MPT
 *  (Multi-Party Token). It sits between transaction-processing code that wants
 *  to be asset-agnostic and the two type-specific leaf modules:
 *  `RippleStateHelpers.h` for IOU trust lines and `MPTokenHelpers.h` for
 *  `MPToken`/`MPTokenIssuance` objects.
 *
 *  Callers pass an `Asset` — a `std::variant<Issue, MPTIssue>` — and the
 *  functions here dispatch via `std::visit` or `Asset::visit` to the correct
 *  lower-level function, returning consistent result types (`STAmount`, `TER`,
 *  `bool`) regardless of asset kind. Adding a new asset type requires only
 *  extending the `Asset` variant and the branches here, not modifying call
 *  sites.
 */
#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <initializer_list>
#include <vector>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Enums for token handling
//
//------------------------------------------------------------------------------

/** Controls how a frozen balance is reported by balance-query functions.
 *
 *  Use `ZeroIfFrozen` in payment paths where a frozen balance must not be
 *  spent. Use `IgnoreFreeze` in cleanup paths that need the real value
 *  regardless of freeze state.
 */
enum class FreezeHandling {
    IgnoreFreeze,   /**< Return the actual balance even if the holding is frozen. */
    ZeroIfFrozen    /**< Return zero when the holding is frozen (the spendable amount). */
};

/** Controls how an unauthorized MPT balance is reported by balance-query functions.
 *
 *  Parallel to `FreezeHandling` but for MPT authorization.  Use
 *  `ZeroIfUnauthorized` when computing the amount an account may legally spend.
 */
enum class AuthHandling {
    IgnoreAuth,         /**< Return the actual balance even if the MPToken is unauthorized. */
    ZeroIfUnauthorized  /**< Return zero when the MPToken is not authorized. */
};

/** Controls whether `accountHolds` reports simple or full spendable balance.
 *
 *  - `SimpleBalance`: the amount the account can spend without going into
 *    debt, i.e. the raw trustline balance (negated to account-centric terms)
 *    for IOU, or the `sfMPTAmount` for MPT.
 *  - `FullBalance`: for IOU, also includes the peer's credit limit so the
 *    account can borrow up to that limit; for the IOU issuer, returns
 *    `STAmount::kMAX_VALUE`; for the MPT issuer, returns
 *    `MaximumAmount - OutstandingAmount`.
 */
enum class SpendableHandling {
    SimpleBalance,  /**< Balance the account can spend without going into debt. */
    FullBalance     /**< Full spendable balance including borrowable credit or issuance capacity. */
};

/** Controls whether the transfer fee is skipped during a send operation.
 *
 *  Typed as `enum class : bool` to prevent accidental transposition with
 *  other boolean parameters at call sites.
 */
enum class WaiveTransferFee : bool {
    No = false, /**< Apply the normal transfer fee. */
    Yes         /**< Skip the transfer fee entirely. */
};

/** Controls whether `accountSend` permits `OutstandingAmount` to transiently
 *  exceed `MaximumAmount` during MPT payment-engine routing.
 *
 *  The payment engine issues tokens first (raising `OutstandingAmount`) and
 *  redeems them in the same transaction (lowering it back). `Yes` raises the
 *  overflow ceiling to `UINT64_MAX` for that transient window. Direct sends
 *  use `No` and enforce the strict `MaximumAmount` cap.
 */
enum class AllowMPTOverflow : bool {
    No = false, /**< Enforce the strict MaximumAmount cap. */
    Yes         /**< Allow transient overflow up to UINT64_MAX during routing. */
};

/** Encodes the three-way authorization-strictness contract.
 *
 *  Determines how `requireAuth` behaves when checking whether an account may
 *  hold or interact with a token:
 *  - `StrongAuth` checks that the holding object (trust line or `MPToken`)
 *    exists *before* asking whether authorization is set.  Returns `tecNO_LINE`
 *    immediately if no holding exists.
 *  - `WeakAuth` skips the existence check, returning `tesSUCCESS` when
 *    authorization is not required even if no holding exists.  Appropriate for
 *    payment path-finding where a line may be created on the fly.
 *  - `Legacy` maps to `StrongAuth` for MPT and `WeakAuth` for IOU, preserving
 *    historical behavior at existing call sites.
 */
enum class AuthType {
    StrongAuth, /**< Existence of the holding object is verified first. */
    WeakAuth,   /**< Holding existence is not required when auth is not needed. */
    Legacy      /**< StrongAuth for MPT; WeakAuth for IOU (historical default). */
};

//------------------------------------------------------------------------------
//
// Freeze checking (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

/** Check whether the issuer of @p asset has activated a global freeze.
 *
 *  Dispatches to the IOU or MPT leaf based on the runtime type of @p asset.
 *  A global freeze on the issuer's `AccountRoot` blocks all holders
 *  simultaneously.
 *
 *  @param view  Read-only ledger view.
 *  @param asset The asset to test.
 *  @return `true` if the issuer has a global freeze in effect.
 */
[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, Asset const& asset);

/** Check whether @p account has an individual freeze on @p asset.
 *
 *  Dispatches to the IOU or MPT leaf based on the runtime type of @p asset.
 *  For IOU, checks the issuer's per-line freeze flag. For MPT, checks the
 *  `lsfMPTLocked` flag on the `MPToken` SLE. Does not check global freeze.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param asset   The asset to test.
 *  @return `true` if the issuer has set an individual freeze on this account.
 */
[[nodiscard]] bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

/** Check whether @p account is frozen for @p asset (global or individual).
 *
 *  Returns `true` if either `isGlobalFrozen` or `isIndividualFrozen` is true
 *  for the given account and asset. Dispatches to the typed IOU or MPT leaf
 *  via `std::visit`.
 *
 *  The `depth` parameter enables recursive vault checking: if @p asset is an
 *  MPT backed by a vault, the vault's underlying asset is checked up to
 *  `maxAssetCheckDepth` levels deep.
 *
 *  @note Recursion is purely defensive. The ledger currently does not allow
 *      nested vaults to be created, so `depth > 0` should not occur in
 *      practice.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param asset   The asset to test.
 *  @param depth   Current recursion depth for vault checking; defaults to 0.
 *  @return `true` if the account cannot move this asset due to any freeze.
 */
[[nodiscard]] bool
isFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth = 0);

/** Convert a freeze check on an IOU to a `TER`.
 *
 *  Returns `tecFROZEN` if `isFrozen` is true for the given account and issue,
 *  `tesSUCCESS` otherwise.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param issue   The IOU to test.
 *  @return `tecFROZEN` if frozen, `tesSUCCESS` otherwise.
 */
[[nodiscard]] TER
checkFrozen(ReadView const& view, AccountID const& account, Issue const& issue);

/** Convert a freeze check on an MPT to a `TER`.
 *
 *  Returns `tecLOCKED` (not `tecFROZEN`) if `isFrozen` is true for the given
 *  account and MPT issuance, `tesSUCCESS` otherwise. The distinct error code
 *  reflects the separate protocol semantics of MPT locking vs IOU freezing.
 *
 *  @param view      Read-only ledger view.
 *  @param account   The account to test.
 *  @param mptIssue  The MPT issuance to test.
 *  @return `tecLOCKED` if frozen/locked, `tesSUCCESS` otherwise.
 */
[[nodiscard]] TER
checkFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

/** Convert a freeze check on any asset to a `TER`.
 *
 *  Dispatches to `checkFrozen(…, Issue)` or `checkFrozen(…, MPTIssue)` based
 *  on the runtime type of @p asset, returning the type-appropriate error code
 *  (`tecFROZEN` for IOU, `tecLOCKED` for MPT).
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param asset   The asset to test.
 *  @return `tecFROZEN` (IOU) or `tecLOCKED` (MPT) if frozen, `tesSUCCESS`
 *      otherwise.
 */
[[nodiscard]] TER
checkFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

/** Check whether any account in @p accounts is frozen for @p issue.
 *
 *  Iterates the list and returns `true` on the first frozen account. Used to
 *  check both sides (taker and maker) of an offer with a single call.
 *
 *  @param view     Read-only ledger view.
 *  @param accounts The accounts to test, e.g. `{takerID, makerID}`.
 *  @param issue    The IOU to test.
 *  @return `true` if any account in the list is frozen for @p issue.
 */
[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Issue const& issue);

/** Check whether any account in @p accounts is frozen for @p asset.
 *
 *  Asset-dispatching overload. Delegates to the IOU or MPT leaf for each
 *  account in the list. The `depth` parameter passes through to `isFrozen`
 *  for vault-backed MPT recursion.
 *
 *  @param view     Read-only ledger view.
 *  @param accounts The accounts to test.
 *  @param asset    The asset to test.
 *  @param depth    Recursion depth for vault checking; defaults to 0.
 *  @return `true` if any account in the list is frozen for @p asset.
 */
[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Asset const& asset,
    int depth = 0);

/** Check whether @p account is deep-frozen for @p mptIssue.
 *
 *  For MPT, deep-freeze semantics are identical to regular freeze: a frozen
 *  MPT holder cannot send or receive. This function delegates to
 *  `isFrozen(view, account, mptIssue, depth)`.
 *
 *  @note For IOU, deep-freeze is a distinct state (`lsfDeepFreeze`) where the
 *      holder cannot send but can still receive. See `isDeepFrozen` in
 *      `RippleStateHelpers.h` for IOU-specific semantics.
 *
 *  @param view      Read-only ledger view.
 *  @param account   The account to test.
 *  @param mptIssue  The MPT issuance to test.
 *  @param depth     Recursion depth for vault checking; defaults to 0.
 *  @return `true` if the account is frozen/locked for this MPT.
 */
[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    int depth = 0);

/** Check whether @p account is deep-frozen for @p asset.
 *
 *  Dispatches to the IOU or MPT leaf via `std::visit`. For MPT, deep-freeze
 *  is equivalent to regular freeze. For IOU, checks the `lsfDeepFreeze` flag,
 *  which prevents sending but allows receiving.
 *
 *  The `depth` parameter enables recursive vault checking up to
 *  `maxAssetCheckDepth` levels.
 *
 *  @note Recursion is purely defensive — nested vaults cannot currently be
 *      created on the ledger.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param asset   The asset to test.
 *  @param depth   Recursion depth for vault checking; defaults to 0.
 *  @return `true` if the account is deep-frozen for @p asset.
 */
[[nodiscard]] bool
isDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth = 0);

/** Convert a deep-freeze check on an MPT to a `TER`.
 *
 *  Returns `tecLOCKED` if `isDeepFrozen` is true, `tesSUCCESS` otherwise.
 *
 *  @param view      Read-only ledger view.
 *  @param account   The account to test.
 *  @param mptIssue  The MPT issuance to test.
 *  @return `tecLOCKED` if deep-frozen, `tesSUCCESS` otherwise.
 */
[[nodiscard]] TER
checkDeepFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

/** Convert a deep-freeze check on any asset to a `TER`.
 *
 *  Dispatches to `checkDeepFrozen(…, Issue)` (`tecFROZEN`) or
 *  `checkDeepFrozen(…, MPTIssue)` (`tecLOCKED`) based on the runtime type of
 *  @p asset.
 *
 *  @param view    Read-only ledger view.
 *  @param account The account to test.
 *  @param asset   The asset to test.
 *  @return `tecFROZEN` (IOU) or `tecLOCKED` (MPT) if deep-frozen,
 *      `tesSUCCESS` otherwise.
 */
[[nodiscard]] TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

//------------------------------------------------------------------------------
//
// Account balance functions (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

/** Return the amount that @p account can spend of the given currency/issuer.
 *
 *  This is the canonical implementation. All other `accountHolds` overloads
 *  ultimately delegate here for the IOU path.
 *
 *  - For XRP: returns `xrpLiquid(view, account, 0, j)` (reserve-adjusted).
 *  - For IOU with `shFULL_BALANCE` when `account == issuer`: returns
 *    `STAmount::kMAX_VALUE` — the issuer has effectively unlimited issuance
 *    capacity.
 *  - For IOU otherwise: reads the trust-line balance from the ledger,
 *    negating it to account-centric terms. If `shFULL_BALANCE` is specified,
 *    also adds the peer's credit limit so the account can draw down that
 *    credit. Returns zero if the line is frozen (when `ZeroIfFrozen`) or does
 *    not exist.
 *
 *  @param view               Read-only ledger view.
 *  @param account            The account whose balance is queried.
 *  @param currency           The IOU currency.
 *  @param issuer             The IOU issuer.
 *  @param zeroIfFrozen       Whether to return zero for frozen balances.
 *  @param j                  Journal for trace logging.
 *  @param includeFullBalance Whether to include borrowable credit or max
 *      issuance capacity; defaults to `SimpleBalance`.
 *  @return The spendable balance, which may be negative (e.g. trust-line debt).
 */
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

/** Return the spendable balance of an IOU for @p account.
 *
 *  Convenience adapter over the `(Currency, AccountID)` overload, extracting
 *  the currency and issuer from @p issue.
 *
 *  @param view               Read-only ledger view.
 *  @param account            The account whose balance is queried.
 *  @param issue              The IOU (currency + issuer).
 *  @param zeroIfFrozen       Whether to return zero for frozen balances.
 *  @param j                  Journal for trace logging.
 *  @param includeFullBalance Balance mode; defaults to `SimpleBalance`.
 *  @return The spendable balance from @p account's perspective.
 */
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

/** Return the spendable balance of an MPT for @p account.
 *
 *  - For the MPT issuer with `shFULL_BALANCE`: returns
 *    `MaximumAmount - OutstandingAmount` (available issuance capacity) via
 *    `availableMPTAmount`.
 *  - For regular holders: reads `sfMPTAmount` from the `MPToken` SLE. Returns
 *    zero if: the `MPToken` SLE does not exist; the token is frozen and
 *    `ZeroIfFrozen` is set; or the token is unauthorized and
 *    `ZeroIfUnauthorized` is set (with `featureSingleAssetVault` gating the
 *    precise auth-check path).
 *  - Under `featureMPTokensV2`, the result passes through
 *    `view.balanceHookMPT` to allow `PaymentSandbox` deferred-credit
 *    interception.
 *
 *  @param view               Read-only ledger view.
 *  @param account            The account whose balance is queried.
 *  @param mptIssue           The MPT issuance.
 *  @param zeroIfFrozen       Whether to zero the balance when frozen/locked.
 *  @param zeroIfUnauthorized Whether to zero the balance when unauthorized.
 *  @param j                  Journal for trace logging.
 *  @param includeFullBalance Balance mode; defaults to `SimpleBalance`.
 *  @return The spendable MPT balance, or zero per the policy flags above.
 */
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

/** Return the spendable balance of any asset for @p account.
 *
 *  Asset-dispatching overload. Delegates to the `Issue` overload (which
 *  ignores `zeroIfUnauthorized`) or the `MPTIssue` overload based on the
 *  runtime type of @p asset.
 *
 *  @param view               Read-only ledger view.
 *  @param account            The account whose balance is queried.
 *  @param asset              The asset to query.
 *  @param zeroIfFrozen       Whether to zero the balance when frozen.
 *  @param zeroIfUnauthorized Whether to zero the balance when unauthorized
 *      (MPT only; ignored for IOU).
 *  @param j                  Journal for trace logging.
 *  @param includeFullBalance Balance mode; defaults to `SimpleBalance`.
 *  @return The spendable balance per the policy flags.
 */
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

/** Return how much of @p saDefault's currency @p id can fund, treating the
 *  issuer as having unlimited supply of their own currency.
 *
 *  For IOU: if `id == saDefault.getIssuer()`, returns `saDefault` directly —
 *  the issuer can always fund an offer for their own currency up to whatever
 *  amount they specify. Otherwise delegates to `accountHolds` with
 *  `SimpleBalance`.
 *
 *  This is the correct semantic for offer matching; prefer `accountFunds` over
 *  `accountHolds` when asking "can this account fund this offer?".
 *
 *  @note `saDefault` must hold an `Issue` (not MPT). Use the `AuthHandling`
 *      overload for asset-agnostic callers.
 *
 *  @param view           Read-only ledger view.
 *  @param id             The account to query.
 *  @param saDefault      The amount (currency + issuer) to check fundability
 *      for.
 *  @param freezeHandling Whether to zero the balance when frozen.
 *  @param j              Journal for trace logging.
 *  @return `saDefault` if @p id is the issuer; otherwise the trust-line
 *      balance, zeroed per @p freezeHandling.
 */
[[nodiscard]] STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j);

/** Asset-agnostic overload of `accountFunds` supporting both IOU and MPT.
 *
 *  For IOU: delegates to the `FreezeHandling`-only overload above.
 *  For MPT: delegates to `accountHolds` with `shFULL_BALANCE`, which
 *  returns the issuer's available issuance capacity or the holder's
 *  `sfMPTAmount`.
 *
 *  @param view           Read-only ledger view.
 *  @param id             The account to query.
 *  @param saDefault      The amount (currency/asset + issuer) to check.
 *  @param freezeHandling Whether to zero the balance when frozen.
 *  @param authHandling   Whether to zero the balance when unauthorized (MPT
 *      only).
 *  @param j              Journal for trace logging.
 *  @return The fundable balance per the policy flags.
 */
[[nodiscard]] STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal j);

/** Return the transfer fee for the asset embedded in @p amount.
 *
 *  Dispatches on `amount.asset()`: for IOU, reads the issuer's transfer rate
 *  from their `AccountRoot`; for MPT, reads the `sfTransferFee` field from
 *  the `MPTokenIssuance` SLE. Both paths return a `Rate` (parts-per-billion).
 *
 *  @param view   Read-only ledger view.
 *  @param amount The amount whose asset determines which fee to look up.
 *  @return The transfer fee as a `Rate`, or `parityRate` if no fee is set.
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, STAmount const& amount);

//------------------------------------------------------------------------------
//
// Holding operations (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

/** Check whether a new holding object (trust line or MPToken) can be created.
 *
 *  For IOU: verifies that the issuer's `AccountRoot` has `lsfDefaultRipple`
 *  set; returns `terNO_RIPPLE` if not, `terNO_ACCOUNT` if the issuer does not
 *  exist, `tesSUCCESS` for XRP. For MPT: delegates to the MPT-specific check.
 *
 *  @note This function is read-only (takes `ReadView`) and is intended to be
 *      called during `preflight`. Any transactor that calls `addEmptyHolding`
 *      in `doApply` must call this function in `preflight` first.
 *
 *  @param view  Read-only ledger view.
 *  @param asset The asset for which a holding would be created.
 *  @return `tesSUCCESS` if a holding can be added; `terNO_RIPPLE`,
 *      `terNO_ACCOUNT`, or an MPT-specific error otherwise.
 */
[[nodiscard]] TER
canAddHolding(ReadView const& view, Asset const& asset);

/** Create an empty holding object (trust line or MPToken) for @p accountID.
 *
 *  Dispatches to `addEmptyHolding(…, Issue)` or `addEmptyHolding(…, MPTIssue)`
 *  based on the runtime type of @p asset. The holding is created with zero
 *  balance and consumes an owner-count reserve slot.
 *
 *  @note The caller must have invoked `canAddHolding` in `preflight` with the
 *      same view and asset to validate preconditions before calling this.
 *
 *  @param view         Mutable ledger view.
 *  @param accountID    The account that will hold the asset.
 *  @param priorBalance The account's XRP balance before this transaction,
 *      used to test reserve sufficiency.
 *  @param asset        The asset to create a holding for.
 *  @param journal      Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, or a `tec`/`tef` error from the
 *      type-specific leaf.
 */
[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal);

/** Delete a zero-balance holding object (trust line or MPToken) for @p accountID.
 *
 *  Dispatches to `removeEmptyHolding(…, Issue)` or
 *  `removeEmptyHolding(…, MPTIssue)` based on the runtime type of @p asset.
 *  The holding must have a zero balance; a non-zero balance returns
 *  `tecHAS_OBLIGATIONS`.
 *
 *  @param view       Mutable ledger view.
 *  @param accountID  The account whose holding should be removed.
 *  @param asset      The asset identifying the holding to remove.
 *  @param journal    Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success; `tecHAS_OBLIGATIONS` if the balance is
 *      non-zero; `tecOBJECT_NOT_FOUND` if no holding exists; or a `tec`/`tef`
 *      error from the type-specific leaf.
 */
[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Asset const& asset,
    beast::Journal journal);

//------------------------------------------------------------------------------
//
// Authorization and transfer checks (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

/** Check whether @p account is authorized to hold or interact with @p asset.
 *
 *  Dispatches to `requireAuth(…, Issue, …)` or `requireAuth(…, MPTIssue, …)`
 *  based on the runtime type of @p asset.
 *
 *  - `StrongAuth`: verifies the holding object exists first; returns
 *    `tecNO_LINE` (IOU) or `tecNO_AUTH` (MPT) if absent.
 *  - `WeakAuth`: skips the existence check; returns success if authorization
 *    is not required even when no holding exists.
 *  - `Legacy`: maps to `StrongAuth` for MPT and `WeakAuth` for IOU to
 *    preserve historical behavior.
 *
 *  @param view     Read-only ledger view.
 *  @param asset    The asset to check authorization for.
 *  @param account  The account to check.
 *  @param authType Authorization strictness; defaults to `AuthType::Legacy`.
 *  @return `tesSUCCESS`, `tecNO_AUTH`, or `tecNO_LINE` depending on the asset
 *      type and authorization state.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    Asset const& asset,
    AccountID const& account,
    AuthType authType = AuthType::Legacy);

/** Check whether @p asset can be transferred from @p from to @p to.
 *
 *  Dispatches to the IOU or MPT leaf. For IOU, checks rippling flags on the
 *  trustlines (returns `terNO_RIPPLE` if both sides block rippling). For MPT,
 *  checks `lsfMPTCanTransfer` on the issuance and the destination's
 *  authorization state.
 *
 *  @param view  Read-only ledger view.
 *  @param asset The asset to transfer.
 *  @param from  The sending account.
 *  @param to    The receiving account.
 *  @return `tesSUCCESS` if the transfer is permitted, or an asset-specific
 *      error (`terNO_RIPPLE`, `tecNO_AUTH`, etc.) otherwise.
 */
[[nodiscard]] TER
canTransfer(ReadView const& view, Asset const& asset, AccountID const& from, AccountID const& to);

//------------------------------------------------------------------------------
//
// Money Transfers (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

/** Send @p saAmount directly without applying transfer fees or limit checks.
 *
 *  Used for IOU redemption, intra-issuer transfers, and MPT moves where the
 *  issuer is one of the endpoints. Dispatches to `directSendNoFeeIOU` for
 *  IOU and `directSendNoFeeMPT` for MPT.
 *
 *  For IOU, @p bCheckIssuer controls whether the function asserts that the
 *  issuer is one of the endpoints. For MPT, the issuer check is not performed
 *  (`bCheckIssuer` must be `false` for MPT).
 *
 *  @note This function is intentionally **not** marked `[[nodiscard]]` for
 *      compatibility with `DirectStep.cpp`, which discards the return value in
 *      certain control paths. All other callers should inspect the result.
 *
 *  @param view          Mutable ledger view.
 *  @param uSenderID     The sending account.
 *  @param uReceiverID   The receiving account.
 *  @param saAmount      The amount to send; its asset determines the dispatch.
 *  @param bCheckIssuer  If `true` (IOU only), asserts that the issuer is one
 *      of the endpoints. Must be `false` for MPT.
 *  @param j             Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success, or a `tec`/`tef` error from the
 *      type-specific leaf.
 */
TER
directSendNoFee(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j);

/** Send @p saAmount from @p from to @p to, applying transfer fees when
 *  applicable.
 *
 *  This is the main asset-transfer entry point for transactors. Dispatches to
 *  `accountSendIOU` or `accountSendMPT` based on the asset type embedded in
 *  @p saAmount. Transfer fees are applied unless `WaiveTransferFee::Yes` is
 *  passed.
 *
 *  The `allowOverflow` flag is forwarded to the MPT path only and controls
 *  whether `OutstandingAmount` may transiently exceed `MaximumAmount` during
 *  the two-phase issue-then-redeem structure used by the payment engine. Direct
 *  sends should use `AllowMPTOverflow::No`.
 *
 *  @param view          Mutable ledger view.
 *  @param from          The sending account.
 *  @param to            The receiving account.
 *  @param saAmount      The amount to send.
 *  @param j             Journal for trace/debug logging.
 *  @param waiveFee      Whether to skip the transfer fee; defaults to `No`.
 *  @param allowOverflow Whether MPT OutstandingAmount may transiently exceed
 *      MaximumAmount; defaults to `No`. Use `Yes` only in payment-engine
 *      routing.
 *  @return `tesSUCCESS` on success, or a `tec`/`tef` error from the
 *      type-specific leaf.
 */
[[nodiscard]] TER
accountSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No,
    AllowMPTOverflow allowOverflow = AllowMPTOverflow::No);

/** A vector of (receiver, amount) pairs used by `accountSendMulti`. */
using MultiplePaymentDestinations = std::vector<std::pair<AccountID, Number>>;

/** Send the same @p asset from @p senderID to multiple @p receivers in one
 *  atomic operation.
 *
 *  Dispatches to `accountSendMultiIOU` or `accountSendMultiMPT` based on
 *  @p asset. Batching avoids repeated round-trips through the ledger state for
 *  the sender's balance and the issuance's `OutstandingAmount` field.
 *
 *  For MPT, the `fixCleanup3_1_3` amendment switches the aggregate
 *  `MaximumAmount` check from a per-iteration stale-snapshot check (pre-fix)
 *  to an exact `uint64_t` running-total check (post-fix) to prevent precision
 *  loss at 19-digit magnitudes near `kMAX_MP_TOKEN_AMOUNT`.
 *
 *  @note `receivers.size()` must be greater than 1 (asserted).
 *
 *  @param view      Mutable ledger view.
 *  @param senderID  The account sending the asset.
 *  @param asset     The asset to send (must match the type of all receiver
 *      amounts).
 *  @param receivers List of (AccountID, Number) destination pairs. All amounts
 *      must be non-negative. Sender-equals-receiver entries are silently
 *      skipped.
 *  @param j         Journal for trace/debug logging.
 *  @param waiveFee  Whether to skip transfer fees; defaults to `No`.
 *  @return `tesSUCCESS` on success, or a `tec`/`tef` error from the
 *      type-specific leaf.
 */
[[nodiscard]] TER
accountSendMulti(
    ApplyView& view,
    AccountID const& senderID,
    Asset const& asset,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No);

/** Transfer XRP directly between two accounts without reserve or fee checks.
 *
 *  XRP has no trust lines, no transfer fees, and no authorization model, so
 *  it bypasses the Asset-dispatch path entirely. Both @p from and @p to must
 *  be non-zero and distinct. Returns `telFAILED_PROCESSING` (open ledger) or
 *  `tecFAILED_PROCESSING` (closed ledger) if the sender's balance is
 *  insufficient.
 *
 *  @param view    Mutable ledger view.
 *  @param from    The sending account; must not be `beast::kZERO`.
 *  @param to      The receiving account; must not be `beast::kZERO`.
 *  @param amount  The XRP amount to transfer; must be native (XRP).
 *  @param j       Journal for trace/debug logging.
 *  @return `tesSUCCESS` on success; `telFAILED_PROCESSING` or
 *      `tecFAILED_PROCESSING` if balance is insufficient; `tefINTERNAL` if
 *      either account SLE cannot be found.
 */
[[nodiscard]] TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j);

}  // namespace xrpl
