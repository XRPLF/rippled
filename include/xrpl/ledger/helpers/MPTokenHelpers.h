/** @file
 *  MPT-specific ledger helper declarations.
 *
 *  Declares the MPT counterpart to `RippleStateHelpers.h`. The asset-agnostic
 *  `TokenHelpers.h` dispatchers route `MPTIssue`-typed calls here via
 *  `std::visit` on the `Asset` variant. In addition to the functions that
 *  mirror IOU trust-line semantics (freeze, transfer rate, holding lifecycle,
 *  authorization), this header exposes operations with no IOU equivalent:
 *  escrow accounting, DEX permission gating, supply-overflow arithmetic, and
 *  the two-phase authorization protocol specific to MPT.
 *
 *  @see RippleStateHelpers.h, TokenHelpers.h
 */
#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <initializer_list>
#include <optional>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Freeze checking (MPT-specific)
//
//------------------------------------------------------------------------------

/** Check whether an entire MPT issuance is globally frozen.
 *
 *  Reads the `MPTokenIssuance` SLE and tests `lsfMPTLocked`. A missing
 *  issuance SLE is treated as unfrozen.
 *
 *  @param view The ledger state to query.
 *  @param mptIssue The MPT issuance to check.
 *  @return `true` if `lsfMPTLocked` is set on the issuance; `false` otherwise.
 */
[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue);

/** Check whether a specific account's MPToken holding is individually frozen.
 *
 *  Reads the per-holder `MPToken` SLE and tests `lsfMPTLocked`. Returns
 *  `false` if no `MPToken` SLE exists for the account (i.e., the account
 *  holds no balance for this issuance).
 *
 *  @param view The ledger state to query.
 *  @param account The account whose holding is checked.
 *  @param mptIssue The MPT issuance to check against.
 *  @return `true` if the account's `MPToken` carries `lsfMPTLocked`;
 *      `false` otherwise.
 */
[[nodiscard]] bool
isIndividualFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

/** Check whether an account's access to an MPT issuance is frozen by any tier.
 *
 *  Applies three checks in order: global issuance lock (`isGlobalFrozen`),
 *  per-account holding lock (`isIndividualFrozen`), and vault pseudo-account
 *  freeze (`isVaultPseudoAccountFrozen`). Short-circuits on the first match.
 *
 *  @param view The ledger state to query.
 *  @param account The account to check.
 *  @param mptIssue The MPT issuance to check against.
 *  @param depth Recursion depth guard forwarded to `isVaultPseudoAccountFrozen`;
 *      bounds pathological nested-vault configurations (currently unreachable
 *      in practice, but defended against up to `maxAssetCheckDepth`).
 *  @return `true` if any freeze tier applies; `false` otherwise.
 */
[[nodiscard]] bool
isFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue, int depth = 0);

/** Check whether any account in a set is frozen for an MPT issuance.
 *
 *  Sequences checks across separate passes to minimize cost: the global freeze
 *  is tested once and short-circuits immediately; individual per-account locks
 *  are checked for every account before the more expensive vault
 *  pseudo-account recursion begins.
 *
 *  @param view The ledger state to query.
 *  @param accounts The set of accounts to check.
 *  @param mptIssue The MPT issuance to check against.
 *  @param depth Recursion depth guard forwarded to `isVaultPseudoAccountFrozen`.
 *  @return `true` if the global freeze is set, or if any account carries an
 *      individual freeze, or if any account is a frozen vault pseudo-account;
 *      `false` otherwise.
 */
[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    MPTIssue const& mptIssue,
    int depth = 0);

//------------------------------------------------------------------------------
//
// Transfer rate (MPT-specific)
//
//------------------------------------------------------------------------------

/** Convert the `sfTransferFee` field of an MPT issuance to the XRPL `Rate` type.
 *
 *  `sfTransferFee` is a `uint16` in the range 0–50,000 representing 0–50%
 *  (units of 0.001%). The encoding maps to `Rate` via
 *  `1,000,000,000 + (10,000 × fee)`, so a 50,000 field value becomes
 *  `1,500,000,000` (50% surcharge over the gross). When `sfTransferFee` is
 *  absent, `parityRate` (1,000,000,000 — no fee) is returned.
 *
 *  @param view The ledger state to query.
 *  @param issuanceID The `MPTokenIssuanceID` of the issuance.
 *  @return The transfer rate as a `Rate` value; `parityRate` when no fee is
 *      configured or the issuance SLE is absent.
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, MPTID const& issuanceID);

//------------------------------------------------------------------------------
//
// Holding checks (MPT-specific)
//
//------------------------------------------------------------------------------

/** Read-only pre-check: verify that an independent holding can be created.
 *
 *  Validates two preconditions before `addEmptyHolding` mutates the ledger:
 *  the `MPTokenIssuance` must exist, and it must carry `lsfMPTCanTransfer`.
 *  Tokens without `lsfMPTCanTransfer` can only move directly between the
 *  issuer and counterparties, making independent holdings meaningless.
 *
 *  @param view The ledger state to query.
 *  @param mptIssue The MPT issuance the caller wants to hold.
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND` if the issuance SLE is absent,
 *      or `tecNO_AUTH` if `lsfMPTCanTransfer` is not set.
 */
[[nodiscard]] TER
canAddHolding(ReadView const& view, MPTIssue const& mptIssue);

//------------------------------------------------------------------------------
//
// Authorization (MPT-specific)
//
//------------------------------------------------------------------------------

/** Core MPToken SLE lifecycle function — create, delete, or toggle authorization.
 *
 *  Behavior depends on `holderID`:
 *  - `holderID` absent (`nullopt`): `account` is the holder. Without
 *    `tfMPTUnauthorize`, a new zero-balance `MPToken` SLE is created and
 *    inserted into the owner directory; the XRP reserve is enforced when
 *    `ownerCount >= 2` (same policy as trust lines). With `tfMPTUnauthorize`,
 *    the existing SLE is erased and the owner count decremented.
 *  - `holderID` set: `account` must be the issuance's issuer. The function
 *    toggles `lsfMPTAuthorized` on the holder's existing `MPToken` SLE.
 *
 *  @param view The mutable ledger state.
 *  @param priorBalance XRP balance before this transaction; used only for the
 *      reserve check when creating a new holding (`holderID` absent and
 *      `tfMPTUnauthorize` not set).
 *  @param mptIssuanceID The issuance being authorized or deauthorized.
 *  @param account Submitting account: the holder (when `holderID` is absent)
 *      or the issuer (when `holderID` is set).
 *  @param journal Logging sink.
 *  @param flags Transaction flags; `tfMPTUnauthorize` selects the
 *      delete/deauthorize path.
 *  @param holderID When set, `account` is the issuer and this is the holder
 *      whose `lsfMPTAuthorized` flag is toggled.
 *  @return `tesSUCCESS`, `tecINSUFFICIENT_RESERVE` if reserves are too low,
 *      `tecDUPLICATE` if the holding already exists, or a `tef` code on
 *      invariant violations.
 */
[[nodiscard]] TER
authorizeMPToken(
    ApplyView& view,
    XRPAmount const& priorBalance,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags = 0,
    std::optional<AccountID> holderID = std::nullopt);

/** Preclaim (read-only) authorization check for an MPT holding.
 *
 *  Issuers are always authorized. When `featureSingleAssetVault` is active,
 *  vault and `LoanBroker` pseudo-accounts are implicitly authorized, and the
 *  check recurses into the vault's underlying asset (bounded by `depth`
 *  vs. `kMAX_ASSET_CHECK_DEPTH`). Domain-based authorization via
 *  `credentials::validDomain` takes precedence over `lsfMPTAuthorized` when
 *  `sfDomainID` is present on the issuance — a passing domain check succeeds
 *  even if no `MPToken` SLE exists.
 *
 *  `WeakAuth` intentionally permits a missing `MPToken` SLE; used in MPToken
 *  V2 flows where the SLE is created on demand during apply.
 *
 *  @note The recursion through vault assets is purely defensive; the ledger
 *      does not currently permit nested-vault MPT configurations.
 *  @param view The ledger state to query (read-only; called in preclaim).
 *  @param mptIssue The MPT issuance being accessed.
 *  @param account The account requesting access.
 *  @param authType Controls leniency toward missing `MPToken` SLEs;
 *      `WeakAuth` allows a missing SLE, `StrongAuth`/`Legacy` require it.
 *  @param depth Current recursion depth; guards against theoretical infinite
 *      recursion through nested vault configurations.
 *  @return `tesSUCCESS` if authorized, `tecOBJECT_NOT_FOUND` if the issuance
 *      is absent, `tecNO_AUTH` if authorization fails, or `tecEXPIRED` if
 *      domain credentials have expired.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy,
    int depth = 0);

/** Enforce account has MPToken to match its authorization (doApply phase).
 *
 *  Must be called when `requireAuth` returned `tesSUCCESS` or `tecEXPIRED`
 *  during preclaim. Re-checks authorization and, if a `sfDomainID` is set on
 *  the issuance, runs `verifyValidDomain` (which deletes expired credentials
 *  as a side effect). When domain authorization succeeds but the account has
 *  no `MPToken` SLE, one is created on the fly using `priorBalance` for the
 *  XRP reserve check.
 *
 *  @note Must not be called for the issuer account.
 *  @param view The mutable ledger state (called in doApply).
 *  @param mptIssuanceID The issuance being accessed.
 *  @param account The holder account; must not be the issuer.
 *  @param priorBalance XRP balance before this transaction; used when lazily
 *      allocating a new `MPToken` SLE for domain-authorized holders.
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, `tecNO_AUTH` if not authorized, `tecEXPIRED` if
 *      credentials have expired, or `tecINSUFFICIENT_RESERVE` if the reserve
 *      check fails during on-demand SLE creation.
 */
[[nodiscard]] TER
enforceMPTokenAuthorization(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    XRPAmount const& priorBalance,
    beast::Journal j);

/** Check whether a transfer between two accounts is permitted by the issuance.
 *
 *  When `lsfMPTCanTransfer` is absent, third-party transfers are blocked.
 *  Transfers where either `from` or `to` is the issuer are always allowed,
 *  mirroring the IOU trust-line policy that lets issuers send and receive
 *  their own tokens unconditionally.
 *
 *  @param view The ledger state to query.
 *  @param mptIssue The MPT issuance involved in the transfer.
 *  @param from The sending account.
 *  @param to The receiving account.
 *  @return `tesSUCCESS` if the transfer is permitted, `tecOBJECT_NOT_FOUND`
 *      if the issuance SLE is absent, or `tecNO_AUTH` if `lsfMPTCanTransfer`
 *      is unset and neither endpoint is the issuer.
 */
[[nodiscard]] TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& from,
    AccountID const& to);

/** Check whether an asset may be traded on the DEX.
 *
 *  Dispatches via `asset.visit`: XRP and IOU assets always succeed; for MPT,
 *  reads the issuance SLE and checks `lsfMPTCanTrade`.
 *
 *  @param view The ledger state to query.
 *  @param asset The asset to check; non-MPT assets always pass.
 *  @return `tesSUCCESS` if trading is permitted, `tecOBJECT_NOT_FOUND` if
 *      the MPT issuance SLE is absent, or `tecNO_PERMISSION` if
 *      `lsfMPTCanTrade` is not set.
 */
[[nodiscard]] TER
canTrade(ReadView const& view, Asset const& asset);

//------------------------------------------------------------------------------
//
// Empty holding operations (MPT-specific)
//
//------------------------------------------------------------------------------

/** Create a zero-balance `MPToken` holding for `accountID`.
 *
 *  Short-circuits to `tesSUCCESS` when the caller is the issuer — issuers
 *  never hold a `MPToken` SLE for their own issuance. For all other accounts,
 *  delegates to `authorizeMPToken`, which enforces the XRP reserve requirement
 *  and inserts the SLE into the owner directory. Returns `tefINTERNAL` if the
 *  issuance SLE is missing or globally locked (invariant violations).
 *
 *  @param view The mutable ledger state.
 *  @param accountID The account requesting the holding.
 *  @param priorBalance XRP balance before this transaction; forwarded to
 *      `authorizeMPToken` for the reserve check.
 *  @param mptIssue The MPT issuance to hold.
 *  @param journal Logging sink.
 *  @return `tesSUCCESS`, `tecDUPLICATE` if a holding already exists,
 *      `tecINSUFFICIENT_RESERVE` if reserves are too low, or `tefINTERNAL`
 *      on issuance-state invariant violations.
 */
[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    MPTIssue const& mptIssue,
    beast::Journal journal);

/** Delete a zero-balance `MPToken` holding.
 *
 *  Requires `sfMPTAmount` to be zero and, when `fixCleanup3_1_3` is enabled,
 *  `sfLockedAmount` to be zero as well; returns `tecHAS_OBLIGATIONS` otherwise.
 *  When `accountID` is the issuer and no `MPToken` SLE exists, returns
 *  `tesSUCCESS` immediately — the normal issuer state. Otherwise delegates to
 *  `authorizeMPToken` with `tfMPTUnauthorize` to erase the SLE and decrement
 *  the owner count.
 *
 *  @param view The mutable ledger state.
 *  @param accountID The account whose holding is being removed.
 *  @param mptIssue The MPT issuance.
 *  @param journal Logging sink.
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND` if no holding exists (and
 *      caller is not the issuer), or `tecHAS_OBLIGATIONS` if the holding
 *      carries a non-zero balance or locked amount.
 */
[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    MPTIssue const& mptIssue,
    beast::Journal journal);

//------------------------------------------------------------------------------
//
// Escrow operations (MPT-specific)
//
//------------------------------------------------------------------------------

/** Move MPT funds from a holder's spendable balance into escrow.
 *
 *  Decrements `sfMPTAmount` and increments `sfLockedAmount` on the sender's
 *  `MPToken` SLE, then increments `sfLockedAmount` on the `MPTokenIssuance`
 *  SLE. `sfOutstandingAmount` on the issuance is deliberately left unchanged —
 *  escrowed tokens remain outstanding until the escrow completes and the
 *  recipient actually receives them. All arithmetic is guarded by
 *  `canSubtract`/`canAdd`.
 *
 *  @param view The mutable ledger state.
 *  @param uGrantorID The account placing tokens in escrow; must not be the issuer.
 *  @param saAmount The MPT amount to lock; must be a valid `MPTIssue` amount.
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, or a `tec`/`tef` error if the issuance or `MPToken`
 *      SLE is missing, the sender is the issuer, or an arithmetic guard fires.
 */
TER
lockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    STAmount const& saAmount,
    beast::Journal j);

/** Release MPT funds from escrow and credit the recipient.
 *
 *  Decrements `sfLockedAmount` on both the sender's `MPToken` SLE and the
 *  `MPTokenIssuance` SLE by `grossAmount`. Then, depending on the receiver:
 *  - Receiver is a third party: `sfMPTAmount` on the receiver's `MPToken` is
 *    incremented by `netAmount`.
 *  - Receiver is the issuer: `sfOutstandingAmount` on the issuance is
 *    decremented by `netAmount` — tokens return to the issuer and retire.
 *  When `fixTokenEscrowV1` is enabled and `grossAmount > netAmount`, the fee
 *  difference is additionally subtracted from `sfOutstandingAmount` because
 *  the fee tokens are effectively burned. All arithmetic is guarded by
 *  `canSubtract`/`canAdd`.
 *
 *  @param view The mutable ledger state.
 *  @param uGrantorID The escrow grantor; must not be the issuer.
 *  @param uGranteeID The escrow grantee (may be the issuer).
 *  @param netAmount The MPT amount credited to the receiver after fees.
 *  @param grossAmount The MPT amount unlocked from escrow (>= `netAmount`).
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, or a `tec`/`tef` error on missing SLEs or
 *      arithmetic guard failure.
 */
TER
unlockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    AccountID const& uGranteeID,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j);

/** Low-level primitive: insert a new `MPToken` SLE and link it into the owner directory.
 *
 *  Inserts the SLE unconditionally without checking for duplicates, enforcing
 *  reserves, or verifying issuance validity. Callers must perform those checks
 *  before invoking this function.
 *
 *  @param view The mutable ledger state.
 *  @param mptIssuanceID The issuance the token belongs to.
 *  @param account The account that will own the `MPToken`.
 *  @param flags Initial `sfFlags` value for the new SLE.
 *  @return `tesSUCCESS`, or `tecDIR_FULL` if the owner directory is full.
 */
TER
createMPToken(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    std::uint32_t const flags);

/** Idempotently ensure a `MPToken` holding exists for `holder`.
 *
 *  Succeeds immediately if `holder` is the issuer or if the `MPToken` SLE
 *  already exists. Otherwise calls `createMPToken` and increments the owner
 *  count. Suitable for apply-phase callers that need to auto-create a holding
 *  without the full reserve and issuance validity checks performed by
 *  `addEmptyHolding`.
 *
 *  @param view The mutable ledger state.
 *  @param mptIssue The MPT issuance the holder will hold.
 *  @param holder The account to receive the holding.
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, `tecDIR_FULL` if the owner directory is full, or
 *      `tecINTERNAL` if the holder's account SLE is missing.
 */
TER
checkCreateMPT(
    xrpl::ApplyView& view,
    xrpl::MPTIssue const& mptIssue,
    xrpl::AccountID const& holder,
    beast::Journal j);

//------------------------------------------------------------------------------
//
// MPT Overflow related
//
//------------------------------------------------------------------------------

/** Return the configured supply cap for an MPT issuance.
 *
 *  Returns `sfMaximumAmount` when present, or `kMAX_MP_TOKEN_AMOUNT` (2^63−1)
 *  when the field is absent, representing an uncapped issuance. The result is
 *  always non-negative and fits in a `std::int64_t`.
 *
 *  @param sleIssuance The `MPTokenIssuance` SLE to query.
 *  @return The maximum allowed outstanding amount.
 */
std::int64_t
maxMPTAmount(SLE const& sleIssuance);

/** Compute remaining issuance headroom from a pre-read SLE.
 *
 *  Returns `maxMPTAmount(sleIssuance) - sfOutstandingAmount`. May transiently
 *  be negative when the payment engine is processing a path step that
 *  temporarily exceeds `MaximumAmount` under `AllowMPTOverflow::Yes`.
 *
 *  @param sleIssuance The `MPTokenIssuance` SLE to query.
 *  @return Headroom as a signed 64-bit integer; may be negative.
 */
std::int64_t
availableMPTAmount(SLE const& sleIssuance);

/** Compute remaining issuance headroom by reading the SLE from the view.
 *
 *  Convenience overload that performs the SLE lookup. Throws
 *  `std::runtime_error` if the issuance SLE is absent — a missing issuance at
 *  this call site indicates a ledger consistency failure rather than a user
 *  error.
 *
 *  @param view The ledger state to query.
 *  @param mptID The `MPTID` of the issuance.
 *  @return Headroom as a signed 64-bit integer; may be negative.
 *  @throws std::runtime_error if the `MPTokenIssuance` SLE is absent.
 */
std::int64_t
availableMPTAmount(ReadView const& view, MPTID const& mptID);

/** Check whether crediting `sendAmount` would overflow the outstanding supply.
 *
 *  Two distinct overflow thresholds are applied based on `allowOverflow`:
 *  1. **`AllowMPTOverflow::No` (direct send):** Enforces the strict cap
 *     `OutstandingAmount + sendAmount ≤ MaximumAmount`. Used by
 *     `directSendNoFee` transactions that bypass the payment engine.
 *  2. **`AllowMPTOverflow::Yes` (payment engine):** Raises the effective
 *     ceiling to `UINT64_MAX` to allow transient in-flight values that exceed
 *     `MaximumAmount` during path routing. A matching redemption step in the
 *     same transaction collapses the overshoot before settlement.
 *
 *  @param sendAmount The proposed additional issuance; must be non-negative.
 *  @param outstandingAmount Current `sfOutstandingAmount` from the issuance SLE.
 *  @param maximumAmount The configured cap (`sfMaximumAmount` or
 *      `kMAX_MP_TOKEN_AMOUNT`).
 *  @param allowOverflow Selects which ceiling to apply.
 *  @return `true` if adding `sendAmount` would exceed the applicable limit.
 */
bool
isMPTOverflow(
    std::int64_t sendAmount,
    std::uint64_t outstandingAmount,
    std::int64_t maximumAmount,
    AllowMPTOverflow allowOverflow);

/** Determine funds available for an issuer to sell in an issuer-owned DEX offer.
 *
 *  During an issuing step (outbound from the issuer), the issuer's
 *  "available" balance is the remaining issuance headroom (`availableMPTAmount`)
 *  adjusted by `balanceHookSelfIssueMPT` to account for any amount already
 *  sold within the same payment. Without this hook, offer-crossing could
 *  allow the issuer to exceed `sfMaximumAmount` across parallel paths in the
 *  same transaction.
 *
 *  @param view The ledger state to query.
 *  @param issue The MPT issuance for which to compute issuer funds.
 *  @return The effective amount the issuer can sell; zero if the issuance SLE
 *      is absent.
 */
[[nodiscard]] STAmount
issuerFundsToSelfIssue(ReadView const& view, MPTIssue const& issue);

/** Track MPT sold by an issuer that owns an MPT sell offer.
 *
 *  Records the cumulative amount sold during the current payment step so that
 *  subsequent calls to `issuerFundsToSelfIssue` return a correctly reduced
 *  available balance. Delegates to `ApplyView::issuerSelfDebitHookMPT` after
 *  computing the current issuance headroom.
 *
 *  @param view The mutable ledger state.
 *  @param issue The MPT issuance being sold.
 *  @param amount The additional amount sold in this step.
 */
void
issuerSelfDebitHookMPT(ApplyView& view, MPTIssue const& issue, std::uint64_t amount);

//------------------------------------------------------------------------------
//
// MPT DEX
//
//------------------------------------------------------------------------------

/** Comprehensive MPT transaction permission check for DEX and payment types.
 *
 *  Verifies in order: the issuer account exists, the `MPTokenIssuance` SLE
 *  exists, the issuance is not globally locked (`lsfMPTLocked`), the
 *  `lsfMPTCanTrade` flag is set, and — for non-issuer accounts — that
 *  `lsfMPTCanTransfer` is set and the account's own `MPToken` is not
 *  individually locked. A missing `MPToken` SLE for a non-issuer is treated
 *  as passing: some transaction types create the `MPToken` on demand and
 *  perform their own missing-token checks.
 *
 *  @note Must not be called with `txType == ttPAYMENT`; use the payment-engine
 *      path's own checks for payments.
 *  @param v The ledger state to query.
 *  @param tx The transaction type being gated.
 *  @param asset The asset involved; non-MPT assets always succeed.
 *  @param accountID The account initiating the transaction.
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND` if the issuance is absent,
 *      `tecNO_ISSUER` if the issuer account is gone, `tecLOCKED` if the
 *      issuance or account is frozen, or `tecNO_PERMISSION` if trading or
 *      transfer is not permitted.
 */
TER
checkMPTTxAllowed(ReadView const& v, TxType tx, Asset const& asset, AccountID const& accountID);

}  // namespace xrpl
