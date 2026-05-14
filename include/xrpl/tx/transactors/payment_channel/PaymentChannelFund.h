#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Adds XRP to an existing payment channel and optionally extends its expiration.
 *
 *  One of three transactors in the payment channel lifecycle (alongside
 *  `PaymentChannelCreate` and `PaymentChannelClaim`). The channel owner may
 *  top up the escrowed XRP balance without closing and reopening the channel,
 *  and may independently extend the voluntary `sfExpiration` deadline.
 *
 *  `ConsequencesFactory` is `Custom` because the consumed XRP is the full
 *  `sfAmount` being deposited — not merely the fee — and the transaction queue
 *  must price it accordingly.
 *
 *  No `preclaim` override is defined; all meaningful ledger preconditions are
 *  checked efficiently inside `doApply` once the channel SLE is in hand.
 */
class PaymentChannelFund : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    explicit PaymentChannelFund(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Report the worst-case XRP consumed so the transaction queue can reserve
     *  the right amount.
     *
     *  Returns a `TxConsequences` whose consumed XRP equals `ctx.tx[sfAmount].xrp()`
     *  — the full deposit, not merely the fee. Pending-transaction accounting
     *  treats this XRP as unavailable until the transaction is included or dropped.
     *
     *  @param ctx  Preflight context providing access to the transaction fields.
     *  @return     `TxConsequences` reflecting fee + full deposit amount.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Validate transaction fields before any ledger access.
     *
     *  Enforces two structural rules:
     *  1. `sfAmount` must be XRP; IOU amounts are not permitted in payment channels.
     *  2. `sfAmount` must be strictly positive.
     *
     *  Everything else is deferred to `doApply`.
     *
     *  @param ctx  Preflight context providing the transaction and current rules.
     *  @return     `tesSUCCESS`, or `temBAD_AMOUNT` if the amount is invalid.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Mutate ledger state to fund the channel and/or extend its expiration.
     *
     *  Executes the following guard sequence:
     *  1. **Channel existence** — looks up the `ltPAYCHAN` SLE by `sfChannel`;
     *     returns `tecNO_ENTRY` if absent.
     *  2. **Expiry short-circuit** — if `parentCloseTime` has passed either
     *     `sfCancelAfter` or `sfExpiration`, calls `closeChannel` and returns its
     *     result. This ensures a stale funding attempt performs useful cleanup
     *     rather than silently failing.
     *  3. **Ownership enforcement** — only the account that originally opened the
     *     channel may fund it; any other submitter receives `tecNO_PERMISSION`.
     *  4. **Expiration extension** — if `sfExpiration` is present in the
     *     transaction, the new value must be at least `parentCloseTime +
     *     sfSettleDelay` (floored by the existing expiration if that is earlier),
     *     ensuring the destination retains time to claim; violates return
     *     `temBAD_EXPIRATION`.
     *  5. **Reserve check** — owner balance must cover `accountReserve(ownerCount)`;
     *     falls short returns `tecINSUFFICIENT_RESERVE`.
     *  6. **Balance check** — owner balance must also cover the deposit on top of
     *     the reserve; falls short returns `tecUNFUNDED`.
     *  7. **Destination existence** — the channel's destination account must still
     *     exist; if deleted, returns `tecNO_DST` to prevent locking XRP into an
     *     unclaimable channel.
     *
     *  On success, `sfAmount` on the channel SLE is incremented and the owner's
     *  `sfBalance` is decremented by the same amount.
     *
     *  @return `tesSUCCESS` on success, or a `tec*`/`tem*`/`tef*` error code.
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor (no-op; reserved for future invariant checks).
     *
     *  @param isDelete  True if the entry is being deleted.
     *  @param before    SLE state before the transaction, or null if new.
     *  @param after     SLE state after the transaction, or null if deleted.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-apply invariant finalizer (no-op; reserved for future invariant checks).
     *
     *  @param tx     The applied transaction.
     *  @param result The TER returned by `doApply`.
     *  @param fee    The fee charged for this transaction.
     *  @param view   Read-only view of the ledger after application.
     *  @param j      Journal for diagnostic logging.
     *  @return       Always `true`; no transaction-specific invariants are checked yet.
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
