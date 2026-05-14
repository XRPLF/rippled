/** @file
 *  Free functions for querying and mutating `ltACCOUNT_ROOT` ledger entries.
 *
 *  Provides the canonical helpers for freeze-state queries, spendable XRP
 *  balance, owner-count bookkeeping, transfer fees, destination-tag
 *  enforcement, and the creation and detection of pseudo-accounts (AMM,
 *  Vault, LoanBroker).  Almost every transaction processor depends on at
 *  least one function here.
 */
#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <set>
#include <vector>

namespace xrpl {

/** Check whether an IOU issuer has the global freeze flag active.
 *
 *  XRP is never frozen; this function returns `false` immediately for the XRP
 *  account.  For any other issuer it reads `lsfGlobalFreeze` from the
 *  account root.  Missing accounts are treated as non-frozen.
 *
 *  @param view   The read-only ledger view to query.
 *  @param issuer The account whose freeze state is to be checked.
 *  @return `true` if `issuer` is a non-XRP account with `lsfGlobalFreeze` set;
 *      `false` otherwise.
 */
[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

/** Compute the spendable XRP balance for an account after reserve deduction.
 *
 *  Queries the account's current balance and owner count through the view's
 *  virtual hook methods (`balanceHookIOU`, `ownerCountHook`) so that
 *  `PaymentSandbox` can overlay uncommitted in-flight changes without any
 *  branching here.  The reserve is then subtracted; if the balance is below
 *  the reserve, the function returns zero rather than a negative amount.
 *
 *  Pseudo-accounts (AMM, Vault, LoanBroker) bypass the reserve calculation
 *  entirely and receive the full balance as spendable XRP, because they
 *  cannot submit transactions and must never be blocked by reserve checks.
 *
 *  @param view          The ledger view to query.
 *  @param id            The account whose liquid XRP balance is computed.
 *  @param ownerCountAdj Signed delta applied to `sfOwnerCount` before the
 *      reserve is calculated.  Pass a positive value when the caller is about
 *      to add ledger entries; pass a negative value when entries are about to
 *      be removed.  This lets callers reason about post-mutation availability
 *      before the state is committed to the view.
 *  @param j             Journal for trace-level diagnostics.
 *  @return The spendable XRP amount, clamped to zero from below.
 */
[[nodiscard]] XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j);

/** Increment or decrement `sfOwnerCount` on an account SLE and notify the view.
 *
 *  Delegates to a file-static helper that clamps the result to
 *  `[0, UINT32_MAX]`, logging at `fatal` severity if either bound would be
 *  exceeded — silent wrapping of the `uint32_t` field would corrupt ledger
 *  state.  After clamping, `view.adjustOwnerCountHook()` is called before the
 *  new value is written; `PaymentSandbox` overrides that hook to track the
 *  high-water-mark count, ensuring subsequent `ownerCountHook` reads use the
 *  most conservative value seen during the payment.
 *
 *  @param view   The mutable view on which the SLE update is recorded.
 *  @param sle    The account SLE to adjust; a null pointer is silently ignored.
 *  @param amount Signed delta to apply to `sfOwnerCount`; must be non-zero.
 *  @param j      Journal for fatal-level diagnostics on overflow or underflow.
 */
void
adjustOwnerCount(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    std::int32_t amount,
    beast::Journal j);

/** Return the IOU transfer fee for an issuer as a `Rate` value.
 *
 *  `Rate` expresses the fee as a fraction of one billion, so a 1% fee is
 *  represented as 1,010,000,000.  If the issuer account does not exist or
 *  has not set `sfTransferRate`, `parityRate` (no fee, i.e., 1,000,000,000)
 *  is returned — callers never need to handle a null case.
 *
 *  @param view   The ledger view to query.
 *  @param issuer The IOU issuer whose transfer fee is requested.
 *  @return The issuer's `Rate`, or `parityRate` if none is configured.
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/** Derive a collision-free pseudo-account `AccountID` from an owner key.
 *
 *  Iterates up to 256 attempts.  Each attempt hashes a counter, the parent
 *  ledger's hash, and `pseudoOwnerKey` through `sha512Half` then
 *  `ripesha_hasher` (RIPEMD-160(SHA-256(...))).  The parent-hash component
 *  prevents precomputation of collisions.  The first candidate address that
 *  has no existing `AccountRoot` in `view` is returned.
 *
 *  @param view          The ledger view used to check for address collisions.
 *  @param pseudoOwnerKey The 256-bit key identifying the pseudo-account owner
 *      (e.g., the AMM or Vault object ID).
 *  @return A collision-free `AccountID`, or `beast::kZERO` if all 256
 *      attempts collided.  `createPseudoAccount` propagates exhaustion as
 *      `tecDUPLICATE`.
 *  @note The 256-attempt cap is consensus-critical and must not be changed
 *      without an amendment, as it determines the pseudo-account address space.
 */
AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey);

/** Return the singleton list of `SField`s that designate a pseudo-account.
 *
 *  Built once at first call by scanning the `ltACCOUNT_ROOT` `SOTemplate`
 *  from `LedgerFormats` and selecting every field whose `SField::sMD_PseudoAccount`
 *  metadata bit is set.  Currently includes `sfAMMID`, `sfVaultID`, and
 *  `sfLoanBrokerID`.  The discovery is fully data-driven: adding a new
 *  pseudo-account type requires only tagging its key field with
 *  `SField::sMD_PseudoAccount` in `sfields.macro` — no manual registration
 *  here is needed.
 *
 *  @return A const reference to the cached vector of pseudo-account fields.
 *  @note Non-active amendments are harmless: the corresponding field will
 *      never be set in practice, so the list remains correct regardless of
 *      which amendments are enabled.
 */
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields();

/** Determine whether an SLE is a pseudo-account (optionally of a specific type).
 *
 *  Returns `true` only when all three conditions hold: `sleAcct` is non-null,
 *  its ledger-entry type is `ltACCOUNT_ROOT`, and at least one pseudo-account
 *  designator field (from `getPseudoAccountFields()`) is present.  When
 *  `pseudoFieldFilter` is non-empty, only fields in the filter are considered,
 *  allowing callers to distinguish AMM pseudo-accounts from Vault
 *  pseudo-accounts.
 *
 *  @param sleAcct          The SLE to inspect; may be null.
 *  @param pseudoFieldFilter Optional subset of pseudo-account fields to match
 *      against.  An empty set (the default) matches any pseudo-account field.
 *  @return `true` if `sleAcct` is a pseudo-account (of a type in the filter
 *      when one is provided); `false` otherwise.
 */
[[nodiscard]] bool
isPseudoAccount(
    std::shared_ptr<SLE const> sleAcct,
    std::set<SField const*> const& pseudoFieldFilter = {});

/** Convenience overload that looks up the account from a `ReadView`.
 *
 *  Reads the `AccountRoot` for `accountId` via `keylet::account()` and
 *  delegates to the SLE overload.
 *
 *  @param view             The ledger view to query.
 *  @param accountId        The account address to look up.
 *  @param pseudoFieldFilter Optional field filter forwarded to the SLE overload.
 *  @return `true` if the account exists and is a pseudo-account matching the
 *      filter; `false` otherwise.
 */
[[nodiscard]] inline bool
isPseudoAccount(
    ReadView const& view,
    AccountID const& accountId,
    std::set<SField const*> const& pseudoFieldFilter = {})
{
    return isPseudoAccount(view.read(keylet::account(accountId)), pseudoFieldFilter);
}

/** Create a protocol-owned pseudo-account `AccountRoot` SLE.
 *
 *  Derives a collision-free address via `pseudoAccountAddress()`, constructs
 *  an `AccountRoot` with zero balance, `lsfDisableMaster | lsfDefaultRipple |
 *  lsfDepositAuth`, and stores `pseudoOwnerKey` in `ownerField`.  When
 *  `featureSingleAssetVault` or `featureLendingProtocol` is enabled,
 *  `sfSequence` is set to `0`; otherwise it is set to the current ledger
 *  sequence.  The zero sequence makes pseudo-accounts visually distinguishable
 *  and provides an extra barrier against accidental transaction submission.
 *
 *  In debug builds, an `XRPL_ASSERT` fires if `ownerField` does not carry the
 *  `SField::sMD_PseudoAccount` flag, catching misuse at development time.
 *
 *  @param view           The mutable ledger view into which the new SLE is
 *      inserted.
 *  @param pseudoOwnerKey The 256-bit key of the owning object (e.g., the AMM
 *      or Vault ledger entry key); stored in `ownerField` on the new SLE.
 *  @param ownerField     The back-link field written on the new SLE; must be
 *      one of the fields returned by `getPseudoAccountFields()`.
 *  @return The newly created SLE on success, or `tecDUPLICATE` if all 256
 *      address derivation attempts collided.
 *  @note Amendment checks are the **caller's** responsibility.  This function
 *      is amendment-neutral by design; callers such as `VaultCreate` and
 *      `LoanBrokerSet` must gate on the relevant feature flag before invoking.
 */
[[nodiscard]] Expected<std::shared_ptr<SLE>, TER>
createPseudoAccount(ApplyView& view, uint256 const& pseudoOwnerKey, SField const& ownerField);

/** Validate a payment destination SLE and its destination-tag requirement.
 *
 *  Returns `tecNO_DST` if `toSle` is null (the destination account does not
 *  exist), and `tecDST_TAG_NEEDED` if the destination has set
 *  `lsfRequireDestTag` but the transaction supplies no tag.  Returns
 *  `tesSUCCESS` otherwise.
 *
 *  @param toSle           The destination account SLE; may be null.
 *  @param hasDestinationTag `true` if the transaction includes a destination
 *      tag field.
 *  @return `tecNO_DST`, `tecDST_TAG_NEEDED`, or `tesSUCCESS`.
 *  @note The ledger enforces the *presence* of a tag but never interprets its
 *      value; semantics (e.g., exchange user IDs) are opaque to the protocol.
 */
[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag);

}  // namespace xrpl
