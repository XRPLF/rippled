#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the PaymentChannelClaim transaction type.
 *
 *  Payment channels are off-ledger scaling constructs: a sender locks XRP
 *  into an on-ledger `ltPAYCHAN` object via `PaymentChannelCreate`, then
 *  issues incrementally larger signed authorization vouchers off-ledger.
 *  Each voucher encodes a new *cumulative* `sfBalance` — "the receiver may
 *  now claim up to N drops total from this channel." When either party
 *  wants to settle, they submit a `PaymentChannelClaim` transaction. This
 *  transactor handles all settlement scenarios: balance claims, voluntary
 *  close, expiry-triggered close, and expiry removal (`tfRenew`).
 *
 *  @note `kCONSEQUENCES_FACTORY` is `Normal` (not `Custom` as in
 *      `PaymentChannelCreate` / `PaymentChannelFund`) because a Claim only
 *      redistributes already-locked XRP and cannot introduce new XRP
 *      obligations for the submitting account.
 *
 *  @see PaymentChannelCreate — opens the channel and locks capacity.
 *  @see PaymentChannelFund — adds XRP to an existing channel.
 */
class PaymentChannelClaim : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit PaymentChannelClaim(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate optional fields on their governing amendments.
     *
     *  Rejects the transaction with `temDISABLED` if `sfCredentialIDs` is
     *  present but the `featureCredentials` amendment is not yet active.
     *  Called by the framework before `preflight`.
     *
     *  @param ctx Preflight context providing the transaction and active rules.
     *  @return `true` if all optional fields are permitted under current rules;
     *      `false` to abort with `temDISABLED`.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the set of legal flag bits for this transaction type.
     *
     *  The base-class `preflight1` enforces this mask before invoking the
     *  transactor-specific `preflight`, so unknown flags are rejected early.
     *
     *  @param ctx Preflight context (unused; present for framework
     *      compatibility).
     *  @return `tfPaymentChannelClaimMask`.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless validation of the transaction fields.
     *
     *  Checks that can be performed without ledger access, in order:
     *  - `sfBalance` and `sfAmount`, when present, must be positive XRP;
     *    `sfBalance` must not exceed `sfAmount`.
     *  - `tfClose` and `tfRenew` are mutually exclusive.
     *  - If `sfSignature` is present, both `sfPublicKey` and `sfBalance`
     *    must also be present (a bare signature is malformed).
     *  - The off-channel payment voucher signature is verified here against
     *    the transaction-supplied public key via
     *    `serializePayChanAuthorization`. This is distinct from the
     *    transaction's own signature (verified by the framework in
     *    `preflight2`); the voucher authorizes the receiver to claim funds,
     *    not the transaction itself.
     *  - Credential field structure is validated via
     *    `credentials::checkFields`.
     *
     *  @note The public key is not yet matched against the channel's stored
     *      key here because `preflight` has no ledger access; that check
     *      occurs in `doApply`.
     *
     *  @param ctx Preflight context.
     *  @return `tesSUCCESS` on success, or a `tem*` code describing the
     *      specific malformation.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validate credentials against live ledger state.
     *
     *  Called only when `featureCredentials` is enabled. Delegates to the
     *  base-class no-op otherwise. Uses `credentials::valid()` to check
     *  that any `sfCredentialIDs` entries exist in the ledger and are not
     *  expired. Credential *cleanup* (removing expired objects) is deferred
     *  to `verifyDepositPreauth` in `doApply`.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS`, or a `ter*`/`tec*` code if credentials are
     *      invalid or expired.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Settle the payment channel on the ledger.
     *
     *  Executes up to four independent behaviors, evaluated in order:
     *
     *  1. **Expiry check (highest priority).** If the ledger's
     *     `parentCloseTime` has passed either `sfCancelAfter` or
     *     `sfExpiration`, `closeChannel()` is called immediately regardless
     *     of other fields or flags. A claim transaction can thus serve as
     *     the trigger that closes an expired channel even when no XRP
     *     transfer is requested.
     *
     *  2. **Permission check.** Only the channel source (`sfAccount`) or
     *     destination (`sfDestination`) may interact with the channel.
     *
     *  3. **Balance claim** (when `sfBalance` is present). Transfers the
     *     incremental difference `reqBalance - chanBalance` to the
     *     destination. `sfBalance` is cumulative: the actual XRP moved is
     *     only the new portion beyond what was already claimed. The
     *     destination must supply `sfSignature`; the source does not need
     *     one. The supplied public key is matched against the channel's
     *     stored `sfPublicKey`. `verifyDepositPreauth` is called before
     *     the transfer to honor any DepositPreauth restrictions.
     *
     *  4. **Renew** (`tfRenew`). Clears `sfExpiration`, retracting a
     *     previously issued close request. Only the source may renew.
     *
     *  5. **Close** (`tfClose`). If the destination is closing, or the
     *     channel is fully drained (`sfBalance == sfAmount`), the channel
     *     closes immediately via `closeChannel()`. If the source is
     *     closing a partially-funded channel, `sfExpiration` is set to
     *     `parentCloseTime + sfSettleDelay`, giving the destination a
     *     guaranteed settlement window; a sooner existing expiration is
     *     never overwritten.
     *
     *  @return `tesSUCCESS` on success, or a `tec*`/`tem*` code:
     *      - `tecNO_TARGET` — channel object not found.
     *      - `tecNO_PERMISSION` — submitter is neither source nor
     *          destination.
     *      - `tecUNFUNDED_PAYMENT` — requested balance does not exceed the
     *          already-settled balance (nothing new to transfer), or exceeds
     *          the channel's total funded capacity.
     *      - `temBAD_SIGNATURE` — destination attempted a claim without
     *          supplying a signature.
     *      - `temBAD_SIGNER` — supplied public key does not match the
     *          channel's stored key.
     *      - `tecNO_DST` — destination account not found.
     */
    TER
    doApply() override;

    /** Per-entry invariant hook (no-op; reserved for future use).
     *
     *  @param isDelete Whether the entry is being deleted.
     *  @param before   SLE state before the transaction.
     *  @param after    SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant check (no-op; reserved for future use).
     *
     *  @param tx     The applied transaction.
     *  @param result The TER returned by `doApply`.
     *  @param fee    The fee deducted.
     *  @param view   Read-only view of the resulting ledger state.
     *  @param j      Journal for diagnostic logging.
     *  @return Always `true` (no invariants enforced yet).
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
