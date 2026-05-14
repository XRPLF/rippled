#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Implements the VaultClawback transaction for forcibly recovering assets or
 *  shares held inside a Vault ledger object.
 *
 *  The transaction operates in one of two mutually exclusive modes, determined
 *  at runtime by comparing the submitting account against vault roles:
 *
 *  - **Share-burn mode** (vault owner, share MPT targeted): burns all of a
 *    holder's vault shares when the vault holds zero assets. This is the
 *    recovery mechanism for stuck vaults where a lending protocol has absorbed
 *    all assets and suffered a total loss, leaving outstanding shares with no
 *    underlying value. Partial burns are not permitted.
 *
 *  - **Asset-clawback mode** (asset issuer, vault asset targeted): recovers
 *    the vault's underlying IOU or MPT assets from the vault pseudo-account
 *    and proportionally destroys the corresponding holder shares. XRP vaults
 *    are excluded; IOU vaults require `lsfAllowTrustLineClawback` (and not
 *    `lsfNoFreeze`) on the issuer's account root; MPT vaults require
 *    `lsfMPTCanClawback` on the issuance.
 *
 *  @note When the asset issuer and vault owner are the same account, the
 *      transaction is ambiguous and `sfAmount` must be supplied explicitly;
 *      omitting it returns `tecWRONG_ASSET`.
 *
 *  @see VaultWithdraw (holder-initiated counterpart that also converts shares
 *      to assets, but requires no external authority)
 */
class VaultClawback : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit VaultClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validate transaction fields before accessing ledger state.
     *
     *  Rejects a zero/empty `sfVaultID`. If `sfAmount` is present it must be
     *  non-negative and must not be XRP, since XRP clawback is categorically
     *  forbidden. A zero `sfAmount` is valid and means "all".
     *
     *  @param ctx Preflight context carrying the transaction and rules.
     *  @return `tesSUCCESS` on success, `temMALFORMED` for an empty vault ID
     *      or an XRP amount, `temBAD_AMOUNT` for a negative amount.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Verify authorization and resolve the operating mode against ledger state.
     *
     *  Loads the vault SLE and share MPT issuance, then resolves the effective
     *  clawback amount via the file-local `clawbackAmount()` helper (an absent
     *  `sfAmount` becomes "all shares" for the vault owner, or "vault asset"
     *  for the issuer).
     *
     *  For **share-burn mode**: enforces that the vault has shares outstanding
     *  but both `sfAssetsTotal` and `sfAssetsAvailable` are zero, and that a
     *  non-zero explicit amount equals the holder's entire share balance.
     *
     *  For **asset-clawback mode**: confirms the submitter is the vault's
     *  asset issuer (not the holder), the asset is not XRP, and the appropriate
     *  clawback flags are set (`lsfAllowTrustLineClawback` without `lsfNoFreeze`
     *  for IOU; `lsfMPTCanClawback` for MPT).
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` on success, `tecNO_ENTRY` if the vault is missing,
     *      `tecWRONG_ASSET` if the amount is ambiguous or targets an unrelated
     *      asset, `tecNO_PERMISSION` for authorization failures,
     *      `tecLIMIT_EXCEEDED` if the vault owner attempts a partial share burn,
     *      `tefINTERNAL` if the share MPT issuance is unexpectedly absent.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the clawback, mutating vault and holder balances.
     *
     *  In **share-burn mode**, sets `sharesDestroyed` to the holder's full
     *  share balance; no assets move. In **asset-clawback mode**, delegates
     *  to `assetsToClawback()` to compute the (shares, assets) pair.
     *
     *  Mutation sequence:
     *  1. Decrements vault `sfAssetsTotal` and `sfAssetsAvailable`.
     *  2. Transfers `sharesDestroyed` shares from holder to vault pseudo-account
     *     (transfer fee waived).
     *  3. Attempts to remove the holder's now-empty MPToken entry via
     *     `removeEmptyHolding()`; tolerates `tecHAS_OBLIGATIONS` silently.
     *  4. If `assetsRecovered > 0`, transfers assets from the vault
     *     pseudo-account to the submitting issuer (transfer fee waived) and
     *     confirms the vault's asset balance has not gone negative.
     *  5. Calls `associateAsset` to re-round stored numeric fields to the
     *     vault asset's precision.
     *
     *  @return `tesSUCCESS` on success, `tecPRECISION_LOSS` if rounding
     *      produced zero shares to destroy, `tecPATH_DRY` on arithmetic
     *      overflow in the share/asset conversion, `tefINTERNAL` on
     *      unexpected ledger-corruption conditions.
     */
    TER
    doApply() override;

    /** Per-entry invariant hook; no transaction-specific checks are registered yet. */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant finalization; no transaction-specific checks
     *  are registered yet.
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

private:
    /** Compute the (assetsRecovered, sharesDestroyed) pair for asset-clawback mode.
     *
     *  When `clawbackAmount` is zero ("all"), converts the holder's entire
     *  share balance to assets via `sharesToAssetsWithdraw`. When non-zero,
     *  performs a double-pass: assets → shares (via `assetsToSharesWithdraw`),
     *  then shares → assets, to account correctly for integer truncation in
     *  the MPT share representation.
     *
     *  If the resulting `assetsRecovered` would exceed `sfAssetsAvailable`
     *  (possible when assets are partially deployed to a lending protocol),
     *  the method clamps to `assetsAvailable` and recomputes shares with
     *  `TruncateShares::Yes` so the re-conversion cannot overshoot the cap.
     *
     *  A legacy pre-`fixCleanup3_1_3` code path is preserved for ledger
     *  replay: zero-amount clawback on old rules skips the `assetsAvailable`
     *  clamp, reproducing the original (incorrect) outcome.
     *
     *  @param vault Mutable vault SLE used for exchange-rate and cap reads.
     *  @param sleShareIssuance Read-only share MPT issuance SLE.
     *  @param holder Account whose shares are being destroyed.
     *  @param clawbackAmount Resolved asset amount to recover; zero means all.
     *  @return `(assetsRecovered, sharesDestroyed)` on success, or an
     *      `Unexpected` `TER`: `tecPATH_DRY` on arithmetic overflow,
     *      `tecINTERNAL` on asset-mismatch or conversion helper failure.
     */
    Expected<std::pair<STAmount, STAmount>, TER>
    assetsToClawback(
        std::shared_ptr<SLE> const& vault,
        std::shared_ptr<SLE const> const& sleShareIssuance,
        AccountID const& holder,
        STAmount const& clawbackAmount);
};

}  // namespace xrpl
