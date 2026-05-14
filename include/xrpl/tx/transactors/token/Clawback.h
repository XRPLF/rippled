#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Executes a ttCLAWBACK transaction on the XRP Ledger.
 *
 *  Allows an issuer to reclaim tokens from a holder's account, supporting
 *  both IOU trust-line tokens and Multi-Purpose Tokens (MPT). The amount
 *  recovered is capped at the holder's spendable balance; any freeze state
 *  the issuer may have applied to the account is bypassed for this purpose.
 *
 *  Clawback is rejected against AMM liquidity-pool accounts (use
 *  `AMMClawback` instead) and, when `featureSingleAssetVault` is active,
 *  against pseudo-accounts.
 *
 *  @note For IOU tokens the holder identity is encoded inside `sfAmount`'s
 *      issuer field; `sfHolder` must be absent. For MPT tokens `sfHolder`
 *      carries the holder account and must be present.
 */
class Clawback : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit Clawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validate the structural correctness of the clawback transaction fields.
     *
     *  Dispatches to a template specialisation based on the asset type carried
     *  in `sfAmount` (IOU `Issue` or `MPTIssue`). For IOUs, rejects if
     *  `sfHolder` is present, if the amount is XRP or non-positive, or if the
     *  issuer and holder are the same account. For MPTs, additionally gates on
     *  the `featureMPTokensV1` amendment and requires `sfHolder` to be present
     *  and the amount to be within `maxMPTokenAmount`.
     *
     *  @param ctx Stateless preflight context carrying the transaction and rules.
     *  @return `tesSUCCESS` if the transaction is structurally valid; a `tem*`
     *      error code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Verify ledger preconditions required for clawback to succeed.
     *
     *  Confirms that both the issuer and holder accounts exist, that the holder
     *  is neither an AMM account nor (when `featureSingleAssetVault` is active)
     *  a pseudo-account, and then dispatches to the asset-type-specific helper.
     *
     *  For IOU tokens the helper checks that `lsfAllowTrustLineClawback` is set
     *  on the issuer and that `lsfNoFreeze` is not set, that the trust line
     *  exists with the correct balance orientation, and that the holder's
     *  spendable balance is non-zero. For MPT tokens it checks `lsfMPTCanClawback`
     *  on the issuance object, that the caller is the issuance's issuer, that the
     *  holder's `MPToken` object exists, and that the spendable balance is
     *  non-zero.
     *
     *  @param ctx Read-only preclaim context providing the current ledger view.
     *  @return `tesSUCCESS` on success; `tecNO_PERMISSION`, `tecNO_LINE`,
     *      `tecAMM_ACCOUNT`, `tecPSEUDO_ACCOUNT`, `tecOBJECT_NOT_FOUND`,
     *      `tecINSUFFICIENT_FUNDS`, or `terNO_ACCOUNT` on failure.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the clawback, moving tokens from the holder back to the issuer.
     *
     *  Calls `directSendNoFee` to transfer `min(spendableBalance, sfAmount)`
     *  tokens without applying any transfer fee. The spendable balance is
     *  computed via `accountHolds` with `fhIGNORE_FREEZE`, so clawback
     *  succeeds up to the available balance even when the account is frozen.
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` if an internal
     *      consistency check fails (should not occur in practice).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor hook for the clawback transactor.
     *
     *  Currently a no-op; reserved for future transaction-specific invariant
     *  checks.
     *
     *  @param isDelete True if the entry is being deleted.
     *  @param before The SLE state before the transaction.
     *  @param after The SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Invariant finalisation hook for the clawback transactor.
     *
     *  Currently a no-op that always returns `true`; reserved for future
     *  transaction-specific invariant checks.
     *
     *  @param tx The transaction being applied.
     *  @param result The TER result code from `doApply`.
     *  @param fee The fee charged for the transaction.
     *  @param view The ledger view after the transaction.
     *  @param j Journal for diagnostic logging.
     *  @return Always `true`; no violations are currently checked.
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
