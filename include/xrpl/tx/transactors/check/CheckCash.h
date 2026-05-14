/** @file
 *  Declares the CheckCash transactor for the XRP Ledger.
 *
 *  `CheckCash` is the only check transactor that transfers value. The other
 *  two (`CheckCreate`, `CheckCancel`) manage the ledger object lifecycle
 *  without moving funds.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Executes a `ttCHECK_CASH` transaction, redeeming a Check for its value.
 *
 *  A Check is an asynchronous, pull-based payment: the sender creates it via
 *  `CheckCreate`, authorizing the named destination to withdraw up to
 *  `sfSendMax` at any future time. `CheckCash` is how the destination collects
 *  those funds. After a successful cash, the Check ledger object is deleted
 *  and the source's owner reserve is released.
 *
 *  The transaction accepts either `sfAmount` (exact delivery) or
 *  `sfDeliverMin` (deliver as much as possible, at least this minimum) —
 *  exactly one must be present. XRP transfers are handled directly; IOU and
 *  MPT transfers route through the `flow()` payment engine, which may
 *  auto-create a trust line or MPT holding on the destination if needed.
 *
 *  `ConsequencesFactory` is `Normal` — the transaction claims its fee on
 *  failure and does not block other transactions in the sender's queue.
 *
 *  @note MPT-denominated checks require the `featureMPTokensV2` amendment,
 *      enforced in `checkExtraFeatures` rather than `preflight`.
 *  @see CheckCreate, CheckCancel
 */
class CheckCash : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Constructs a `CheckCash` transactor bound to the given apply context. */
    explicit CheckCash(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Amendment guard for MPT-denominated checks.
     *
     *  Returns `false` (blocking the transaction) when either `sfAmount` or
     *  `sfDeliverMin` names an MPT asset and `featureMPTokensV2` is not yet
     *  active. IOU and XRP checks pass unconditionally.
     *
     *  @param ctx  The preflight context carrying the transaction and active
     *      rule set.
     *  @return `true` if the transaction may proceed to `preflight`; `false`
     *      if it should be rejected because the required amendment is not
     *      enabled.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless structural validation.
     *
     *  Enforces two invariants without accessing ledger state:
     *  1. Exactly one of `sfAmount` or `sfDeliverMin` is present; both or
     *     neither is `temMALFORMED`.
     *  2. The chosen amount passes `isLegalNet()`, is strictly positive, and
     *     does not name `badAsset()`.
     *
     *  @param ctx  The preflight context (no ledger access).
     *  @return `tesSUCCESS` if the transaction is structurally valid.
     *  @return `temMALFORMED` if both or neither amount field is present, or
     *      if the amount is zero, negative, or an illegal asset.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger-state validation.
     *
     *  Resolves the Check via `sfCheckID` and verifies all preconditions
     *  without modifying state:
     *  - Check exists (`tecNO_ENTRY` otherwise).
     *  - Submitter is the check's destination (`tecNO_PERMISSION` otherwise).
     *  - Source and destination are different accounts (`tecINTERNAL` if not —
     *    this is a "should never happen" path guarded by `LCOV_EXCL`).
     *  - Destination tag is present if the destination account requires it.
     *  - Check has not expired (`tecEXPIRED` otherwise).
     *  - Requested asset and issuer match the check's `sfSendMax`; requested
     *    amount does not exceed `sfSendMax`.
     *  - Source has sufficient available funds. For XRP checks, one reserve
     *    increment is added to the available balance to account for the reserve
     *    that will be released when the Check is deleted.
     *  - For IOU assets not self-issued by the destination: issuer account
     *    exists, trust line authorization is satisfied, and the destination's
     *    trust line is not frozen.
     *  - For MPT assets: destination's holding passes `requireAuth` (weak),
     *    is not frozen (`isFrozen`), and the asset permits DEX trading
     *    (`canTrade`).
     *
     *  @param ctx  The preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS` if all conditions are met.
     *  @return `tecNO_ENTRY` if the Check object does not exist.
     *  @return `tecNO_PERMISSION` if the submitter is not the check's
     *      destination.
     *  @return `tecINTERNAL` if source and destination are the same account
     *      (ledger-corruption sentinel, should be unreachable).
     *  @return `tecEXPIRED` if the check has passed its expiration time.
     *  @return `tecINSUFFICIENT_FUNDS` if the source cannot cover the
     *      requested amount.
     *  @return `tecFROZEN` / `tecLOCKED` if the relevant trust line or MPT
     *      holding is frozen or locked.
     *  @return `tecNO_AUTH` if authorization is required but not granted.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Commits the check redemption to the ledger.
     *
     *  All mutations occur inside a `PaymentSandbox`; `psb.apply()` is called
     *  only after every step succeeds, ensuring atomicity.
     *
     *  **XRP path** — handled directly (not via `flow()`): `xrpLiquid()` is
     *  called with a `-1` reserve adjustment to account for the reserve
     *  released when the Check is deleted. For `sfDeliverMin`, delivery is
     *  `max(DeliverMin, min(sendMax, srcLiquid))`.
     *
     *  **IOU/MPT path** — routed through `flow()`. If no trust line exists
     *  between the destination and the issuer, one is created automatically
     *  (the destination is signing, so their intent is unambiguous); its limit
     *  is temporarily raised to `cMaxValue` for the duration of the `flow()`
     *  call, then restored via `scope_exit`. For MPT assets without an
     *  existing holding, `checkCreateMPT()` initializes the slot. For
     *  `sfDeliverMin`, the ask amount passed to `flow()` is `cMaxValue / 2`
     *  to tolerate up to 200% gateway transfer rates without overflow.
     *
     *  On success: the Check is removed from both the owner and destination
     *  account directories, the source's owner count is decremented, and the
     *  Check SLE is erased.
     *
     *  @return `tesSUCCESS` on successful fund transfer and Check deletion.
     *  @return `tecNO_ENTRY` if the Check SLE cannot be peeked (should not
     *      occur after passing `preclaim`).
     *  @return `tecPATH_PARTIAL` if `sfDeliverMin` is specified and `flow()`
     *      cannot deliver the minimum amount.
     *  @return `tecPATH_DRY` if the payment path is exhausted.
     *  @return `tefBAD_LEDGER` if a directory removal fails due to ledger
     *      corruption (unreachable in a correctly functioning ledger).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor hook.
     *
     *  No transaction-specific invariants are currently registered for
     *  `CheckCash`; this is a no-op stub reserved for future use.
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
     *  `CheckCash`; always returns `true`. Reserved for future use.
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
