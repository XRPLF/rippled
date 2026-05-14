#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Transactor for opening a unidirectional XRP payment channel (`ttPAYCHAN_CREATE`).
 *
 * Payment channels let two parties exchange a stream of XRP micropayments
 * off-ledger via signed claim messages, while committing only the open and
 * close operations to the global ledger. This transactor handles the first
 * on-ledger step: locking the sender's XRP into a new `PayChannel` SLE and
 * registering it in both parties' owner directories.
 *
 * Pipeline contract:
 * - `makeTxConsequences` — reports the full `sfAmount` as consumed XRP so the
 *   transaction queue accurately prices the account's locked funds.
 * - `preflight` — field-level validation only; no ledger access.
 * - `preclaim` — read-only ledger checks (reserve, destination existence,
 *   permission flags, pseudo-account guard).
 * - `doApply` — constructs the `PayChannel` SLE, inserts dual directory
 *   entries, and debits the sender's balance.
 *
 * `ConsequencesFactory` is `Custom` because the channel locks the full
 * `sfAmount` of XRP beyond just the fee, unlike a `Normal` transactor.
 */
class PaymentChannelCreate : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    explicit PaymentChannelCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Report the worst-case XRP consumed so the transaction queue can reserve
     * the right amount.
     *
     * Returns a `TxConsequences` whose consumed XRP is `ctx.tx[sfAmount].xrp()`
     * — the full channel funding, not merely the fee. This distinction matters
     * for pending-transaction accounting: the account cannot spend the escrowed
     * XRP while this transaction awaits inclusion.
     *
     * @param ctx  Preflight context providing access to the transaction fields.
     * @return     `TxConsequences` reflecting fee + full channel amount.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /**
     * Validate transaction fields before any ledger access.
     *
     * Enforces three structural invariants:
     * 1. `sfAmount` must be a positive XRP value; IOUs are not permitted in
     *    payment channels.
     * 2. `sfAccount` must differ from `sfDestination`; self-funded channels
     *    are rejected with `temDST_IS_SRC`.
     * 3. `sfPublicKey` must parse as a known key type (secp256k1 or Ed25519);
     *    this key signs off-ledger claims and cannot be changed after creation.
     *
     * @param ctx  Preflight context providing the transaction and current rules.
     * @return     `tesSUCCESS`, or a `tem*` error code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Validate ledger state before mutating it.
     *
     * Checks (in order):
     * - Sender account exists.
     * - Sender balance covers the new owner reserve (`tecINSUFFICIENT_RESERVE`)
     *   and the full channel amount on top of that (`tecUNFUNDED`). The two
     *   codes are intentionally distinct: `tecINSUFFICIENT_RESERVE` means the
     *   account cannot hold another ledger object at all; `tecUNFUNDED` means
     *   it can hold the object but cannot fund it at the requested level.
     * - Destination account exists (`tecNO_DST`).
     * - Destination has not set `lsfDisallowIncomingPayChan` (`tecNO_PERMISSION`).
     * - Destination tag is present when required by `lsfRequireDestTag`
     *   (`tecDST_TAG_NEEDED`).
     * - Destination is not a pseudo-account (`tecNO_PERMISSION`). This check
     *   is not amendment-gated because pseudo-account discriminator fields are
     *   themselves only written under amendment guards, so the check's
     *   behaviour automatically tracks the active amendments.
     *
     * @param ctx  Preclaim context providing read-only ledger access.
     * @return     `tesSUCCESS`, or a `tec*`/`ter*` error code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Construct the `PayChannel` SLE and commit all ledger mutations.
     *
     * Performs the following steps in order:
     * 1. **Expiry check** (under `fixPayChanCancelAfter`): if `sfCancelAfter`
     *    is present and already past the ledger's `parentCloseTime`, returns
     *    `tecEXPIRED`. This runs in `doApply`, not `preclaim`, because the
     *    canonical close time is only fully settled at apply time.
     * 2. **Channel SLE construction**: creates a `PayChannel` object at
     *    `keylet::payChan(account, dst, tx.getSeqValue())`. Using the
     *    sequence-or-ticket value as part of the key makes the channel ID
     *    deterministic before the transaction lands — off-ledger parties can
     *    reference the channel in signed claims before confirmation. The SLE
     *    captures total escrowed funds (`sfAmount`), running paid balance
     *    initialised to zero (`sfBalance`), both account IDs, settle delay,
     *    signing public key, and optional cancel-after/tag fields. Under
     *    `fixIncludeKeyletFields`, `sfSequence` is also stored so the keylet
     *    can be reconstructed from the SLE alone.
     * 3. **Dual directory registration**: inserts the channel into the sender's
     *    owner directory (`sfOwnerNode`) and the recipient's owner directory
     *    (`sfDestinationNode`), enabling efficient removal on closure.
     * 4. **Balance and owner count update**: decrements sender `sfBalance` by
     *    the channel amount and calls `adjustOwnerCount(..., +1, ...)` to
     *    raise the sender's minimum reserve.
     *
     * @return `tesSUCCESS` on success, or a `tef*`/`tec*` code on failure.
     */
    TER
    doApply() override;

    /**
     * Per-entry invariant visitor (reserved for future transaction-specific
     * invariant checks; currently a no-op).
     *
     * @param isDelete  True if the entry is being deleted.
     * @param before    SLE state before the transaction, or null if new.
     * @param after     SLE state after the transaction, or null if deleted.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Post-apply invariant finalizer (reserved for future transaction-specific
     * invariant checks; currently always returns `true`).
     *
     * @param tx     The applied transaction.
     * @param result The TER returned by `doApply`.
     * @param fee    The fee charged for this transaction.
     * @param view   Read-only view of the ledger after application.
     * @param j      Journal for diagnostic logging.
     * @return       `true` if all invariants pass; `false` to veto the result.
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
