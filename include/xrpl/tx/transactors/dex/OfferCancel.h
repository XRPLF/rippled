#pragma once

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the OfferCancel transaction type.
 *
 *  Removes a standing DEX offer identified by its original sequence number.
 *  The implementation is intentionally thin: field validation is minimal,
 *  and the actual ledger teardown — unlinking from the owner directory,
 *  removing the order-book directory entry, updating owner counts — is
 *  delegated entirely to the shared `offerDelete` helper.
 *
 *  Cancellation is idempotent: if the target offer no longer exists
 *  (consumed by a trade, previously cancelled, or expired), `doApply`
 *  returns `tesSUCCESS` without modifying any ledger state. The fee is
 *  still charged — the transaction was valid and processed.
 *
 *  `kCONSEQUENCES_FACTORY` is `Normal` because cancellation never reserves
 *  owner reserves or locks funds beyond the transaction fee itself, so the
 *  framework can model its consequences without per-transaction computation.
 */
class OfferCancel : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit OfferCancel(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validate static transaction fields before ledger access.
     *
     *  Rejects the transaction with `temBAD_SEQUENCE` if `sfOfferSequence`
     *  is zero. No valid offer can carry sequence number zero, so a zero
     *  value is always a client error. All other field checks (account,
     *  fee, flags, signature) are handled by `preflight1`/`preflight2` in
     *  the base-class pipeline.
     *
     *  @param ctx Preflight context carrying the transaction and rules.
     *  @return `tesSUCCESS` if the sequence field is non-zero;
     *      `temBAD_SEQUENCE` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Check ledger state before applying the cancellation.
     *
     *  Reads the submitter's account entry and enforces a temporal ordering
     *  invariant: `sfOfferSequence` must be strictly less than the account's
     *  current sequence number. If the offer sequence is greater than or
     *  equal to the account sequence, the offer could not yet exist on the
     *  ledger, and the transaction is rejected.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the account exists and the sequence ordering
     *      is valid; `terNO_ACCOUNT` if the submitter's account is not found
     *      (highly unusual at this stage); `temBAD_SEQUENCE` if
     *      `sfOfferSequence` is not strictly less than the account's current
     *      sequence number.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Remove the target offer from the ledger.
     *
     *  Resolves the offer via `keylet::offer(account_, offerSequence)`. If
     *  the offer exists, delegates removal to `offerDelete`, which unlinks
     *  it from the owner and order-book directories and adjusts the owner
     *  count. If the offer is not found, returns `tesSUCCESS` without any
     *  state changes — the offer may have already been consumed by a
     *  crossing trade, cancelled, or expired.
     *
     *  @return Result of `offerDelete` if the offer was found;
     *      `tesSUCCESS` if the offer was already absent from the ledger;
     *      `tefINTERNAL` (unreachable in practice) if the account SLE is
     *      missing.
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor (no-op; reserved for future work). */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalize invariant checks (no-op; reserved for future work).
     *
     *  @return Always `true`.
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
