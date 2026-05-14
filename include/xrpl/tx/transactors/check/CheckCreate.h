/** @file
 *  Declares the CheckCreate transactor for the XRP Ledger.
 *
 *  A Check is a deferred payment authorization: the sender commits a
 *  `sfSendMax` amount to a named destination, which may later redeem it
 *  via `CheckCash` or cancel it via `CheckCancel`. This header defines the
 *  interface; the validation and ledger-mutation logic lives in
 *  `CheckCreate.cpp`.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Executes a `ttCHECK_CREATE` transaction, writing a Check object to the
 *  ledger.
 *
 *  A Check is a pull-based payment authorization: the drawer (source account)
 *  specifies a destination and a maximum spend (`sfSendMax`), but no funds
 *  move at creation time. The destination may later redeem the Check via
 *  `CheckCash`, or either party may remove it via `CheckCancel`.
 *
 *  Creating a Check adds one object to the sender's owner directory and
 *  increments their owner count, increasing their reserve requirement by one
 *  increment. The reserve is checked against `preFeeBalance_` so that the
 *  transaction fee may be paid from reserve funds while still requiring full
 *  reserve coverage for the new object.
 *
 *  `kCONSEQUENCES_FACTORY` is `Normal` — the transaction claims its fee on
 *  failure and does not block other transactions in the sender's queue.
 *
 *  @note MPT-denominated checks require the `featureMPTokensV2` amendment,
 *      enforced in `checkExtraFeatures` rather than in `preflight`.
 *  @see CheckCash, CheckCancel
 */
class CheckCreate : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Constructs a `CheckCreate` transactor bound to the given apply context. */
    explicit CheckCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Amendment guard for MPT-denominated checks.
     *
     *  Returns `false` (blocking the transaction) when `sfSendMax` names an
     *  MPT asset and `featureMPTokensV2` is not yet active. IOU and XRP checks
     *  pass unconditionally. This is the correct place for amendment gating,
     *  keeping feature checks separate from the core field validation in
     *  `preflight`.
     *
     *  @param ctx  The preflight context carrying the transaction and active
     *      rule set.
     *  @return `true` if the transaction may proceed to `preflight`; `false`
     *      if it should be rejected because the required amendment is not
     *      enabled.
     */
    static bool
    checkExtraFeatures(xrpl::PreflightContext const& ctx);

    /** Stateless structural validation.
     *
     *  Validates the transaction fields without accessing ledger state:
     *  - Rejects self-addressed checks (`sfAccount == sfDestination`) with
     *    `temREDUNDANT`.
     *  - Rejects `sfSendMax` that fails `isLegalNet()`, is non-positive, or
     *    names `badAsset()`.
     *  - Rejects a zero `sfExpiration` value; a non-zero value means "expires
     *    at that time", while absence means "never expires".
     *
     *  @param ctx  The preflight context (no ledger access).
     *  @return `tesSUCCESS` if the transaction is structurally valid.
     *  @return `temREDUNDANT` if the source and destination accounts are
     *      identical.
     *  @return `temBAD_AMOUNT` if `sfSendMax` is zero, negative, or otherwise
     *      malformed.
     *  @return `temBAD_CURRENCY` if `sfSendMax` names `badAsset()`.
     *  @return `temBAD_EXPIRATION` if `sfExpiration` is present and zero.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger-state validation.
     *
     *  Checks all preconditions that require ledger access:
     *  - Destination account exists.
     *  - Destination has not set `lsfDisallowIncomingCheck`.
     *  - Destination is not a pseudo-account (not amendment-gated; the
     *    pseudo-account discriminator fields are themselves amendment-gated).
     *  - Destination tag is present when `lsfRequireDestTag` is set on the
     *    destination.
     *  - For non-XRP `sfSendMax`: the asset is not globally frozen; IOU
     *    trustlines from sender to issuer and from issuer to destination are
     *    not individually frozen; MPT holdings for sender and destination (when
     *    neither is the issuer) are not locked. A missing trustline on the
     *    sender side is permitted — the check is speculative and the trustline
     *    need not exist at creation time.
     *  - `sfExpiration`, if present, has not already passed.
     *  - The asset is tradeable via `canTrade`.
     *
     *  @param ctx  The preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS` if all conditions are met.
     *  @return `tecNO_DST` if the destination account does not exist.
     *  @return `tecNO_PERMISSION` if the destination has set
     *      `lsfDisallowIncomingCheck` or is a pseudo-account.
     *  @return `tecDST_TAG_NEEDED` if the destination requires a destination
     *      tag and none was supplied.
     *  @return `tecFROZEN` if an IOU trustline (global or per-line) is frozen.
     *  @return `tecLOCKED` if the MPT asset or an MPT holding is locked.
     *  @return `tecEXPIRED` if the specified expiration has already passed.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Commits the Check creation to the ledger.
     *
     *  Enforces the reserve requirement against `preFeeBalance_` (the
     *  pre-fee balance captured by the base class), deliberately allowing the
     *  transaction fee to be paid from reserve funds while still requiring full
     *  reserve coverage for the new Check object.
     *
     *  Constructs the Check SLE keyed by `keylet::check(account_, seq)`, where
     *  `seq` is the transaction's sequence or ticket value. Populates all
     *  optional fields (`sfSourceTag`, `sfDestinationTag`, `sfInvoiceID`,
     *  `sfExpiration`) only when present in the transaction. The Check is
     *  inserted into both the sender's and destination's owner directories;
     *  `sfOwnerNode` and `sfDestinationNode` record the respective directory
     *  page indices for O(1) removal. On success, `adjustOwnerCount`
     *  increments the sender's owner count by 1.
     *
     *  @return `tesSUCCESS` on successful Check creation.
     *  @return `tecINSUFFICIENT_RESERVE` if the sender's pre-fee balance
     *      cannot cover the incremented reserve requirement.
     *  @return `tecDIR_FULL` if either owner directory has no room for a new
     *      entry (ledger-corruption sentinel, marked `LCOV_EXCL`).
     *  @return `tefINTERNAL` if the sender's `AccountRoot` SLE cannot be
     *      peeked (ledger-corruption sentinel, marked `LCOV_EXCL`).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor hook.
     *
     *  No transaction-specific invariants are currently registered for
     *  `CheckCreate`; this is a no-op stub reserved for future use.
     *
     *  @param isDelete  Whether the entry is being deleted.
     *  @param before    SLE state before the transaction.
     *  @param after     SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant finalizer.
     *
     *  No transaction-specific invariants are currently registered for
     *  `CheckCreate`; always returns `true`. Reserved for future use.
     *
     *  @param tx      The applied transaction.
     *  @param result  The TER result from `doApply`.
     *  @param fee     The fee deducted.
     *  @param view    A read-only view of the resulting ledger state.
     *  @param j       Journal for diagnostic logging.
     *  @return `true` unconditionally.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
