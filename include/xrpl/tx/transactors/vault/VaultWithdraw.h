#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttVAULT_WITHDRAW` transaction type.
 *
 *  Redeems vault-share MPTokens for the underlying pooled asset — the inverse
 *  of `VaultDeposit`. The submitter specifies `sfAmount` in either the vault's
 *  underlying asset or its share MPT; the complementary quantity is computed
 *  via the vault's current share-to-asset exchange rate.
 *
 *  The three-phase pipeline is:
 *  - `preflight` — rejects a zero `sfVaultID`, a non-positive `sfAmount`, and
 *    a zero explicit `sfDestination` without accessing the ledger.
 *  - `preclaim` — read-only checks: vault existence, asset denomination,
 *    transferability, withdrawal limits (with share-to-asset conversion when
 *    `fixSecurity3_1_3` is active), destination authorization, and freeze state.
 *  - `doApply` — burns shares, decrements `sfAssetsTotal`/`sfAssetsAvailable`,
 *    and credits the underlying asset to the destination account.
 *
 *  @note Possession of vault shares is treated as standing authorization to
 *      withdraw. The `lsfVaultPrivate` flag is intentionally not re-checked
 *      in `doApply`; holding a share proves prior authorized deposit.
 *  @note Transfer fees are waived when returning shares to the vault
 *      pseudo-account (`WaiveTransferFee::Yes`) because shares are internal
 *      bookkeeping tokens, not economic transfers.
 *  @see VaultDeposit, VaultCreate, VaultHelpers.h
 */
class VaultWithdraw : public Transactor
{
public:
    /** Standard fee and sequence consequences; does not block queued transactions. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit VaultWithdraw(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Perform stateless, ledger-free field validation.
     *
     *  Checks:
     *  - `sfVaultID` must be non-zero; returns `temMALFORMED` otherwise.
     *  - `sfAmount` must be positive; returns `temBAD_AMOUNT` otherwise.
     *  - If `sfDestination` is present, it must be non-zero; returns
     *    `temMALFORMED` otherwise.
     *
     *  @param ctx  Preflight context carrying the transaction and active rules.
     *  @return `tesSUCCESS`, `temMALFORMED`, or `temBAD_AMOUNT`.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Perform read-only ledger checks after signature verification.
     *
     *  Validates the following constraints in order, returning the first
     *  failing code:
     *  - Vault existence: the object identified by `sfVaultID` must exist;
     *    returns `tecNO_ENTRY` otherwise.
     *  - Asset denomination: `sfAmount` must be denominated in either the
     *    vault's `sfAsset` or its share MPT; returns `tecWRONG_ASSET` otherwise.
     *  - Transferability: the vault asset must be transferable from the vault
     *    pseudo-account to the destination; returns the result of `canTransfer`
     *    otherwise.
     *  - Withdrawal policy: only `vaultStrategyFirstComeFirstServe` is
     *    supported; any other value returns `tefINTERNAL` (invariant violation).
     *  - Withdrawal limits: when `fixSecurity3_1_3` is active and the amount is
     *    share-denominated, shares are first converted to an equivalent asset
     *    amount before calling `canWithdraw`. Pre-amendment, share-denominated
     *    requests bypassed this check entirely. Overflow during conversion
     *    returns `tecPATH_DRY`.
     *  - Destination authorization: `WeakAuth` (trust line or MPToken may be
     *    created in `doApply`) when withdrawing to `sfAccount`; `StrongAuth`
     *    (trust line or MPToken must already exist) when withdrawing to a third
     *    party.
     *  - Freeze / lock: returns a freeze error if the vault asset is frozen for
     *    the destination, or if the vault's share MPT is frozen for the
     *    submitting account.
     *
     *  @param ctx  Preclaim context carrying a read-only ledger view.
     *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecWRONG_ASSET`, `tecPATH_DRY`,
     *      `tefINTERNAL`, `tecFROZEN`, `tecLOCKED`, or a code from
     *      `canTransfer` / `canWithdraw` / `requireAuth` / `checkFrozen`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the vault withdrawal to the mutable ledger view.
     *
     *  Executes the following steps atomically:
     *  1. Resolves the vault and share issuance SLEs with write access.
     *  2. If `sfAmount` is asset-denominated: calls `assetsToSharesWithdraw` to
     *     find `sharesRedeemed`, returns `tecPRECISION_LOSS` if the result is
     *     zero (dust prevention), then back-computes `assetsWithdrawn` via
     *     `sharesToAssetsWithdraw`. If `sfAmount` is share-denominated: uses the
     *     amount directly as `sharesRedeemed` and derives `assetsWithdrawn`.
     *  3. Returns `tecPATH_DRY` if conversion throws `std::overflow_error`.
     *  4. Verifies the submitter holds at least `sharesRedeemed` via
     *     `accountHolds`; returns `tecINSUFFICIENT_FUNDS` if not.
     *  5. Checks `sfAssetsAvailable` (not the raw pseudo-account balance)
     *     against `assetsWithdrawn`; returns `tecINSUFFICIENT_FUNDS` if
     *     insufficient. Using `sfAssetsAvailable` correctly excludes assets
     *     pledged to external lending positions.
     *  6. Decrements both `sfAssetsTotal` and `sfAssetsAvailable` on the vault
     *     by `assetsWithdrawn`.
     *  7. Transfers `sharesRedeemed` from the submitter back to the vault
     *     pseudo-account via `accountSend` with `WaiveTransferFee::Yes`.
     *  8. Attempts to remove the submitter's now-empty share MPToken via
     *     `removeEmptyHolding`; `tecHAS_OBLIGATIONS` (balance non-zero) is
     *     silently tolerated. Skipped when the submitter is the vault owner.
     *  9. Calls `doWithdraw` to credit `assetsWithdrawn` from the vault
     *     pseudo-account to the destination account.
     *  10. Calls `associateAsset` on the vault SLE — must be the final write to
     *      round stored numeric values to the asset's precision.
     *
     *  @return `tesSUCCESS`, `tecPRECISION_LOSS`, `tecPATH_DRY`,
     *      `tecINSUFFICIENT_FUNDS`, or a `tec*` / `ter*` code from the
     *      underlying helpers.
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op placeholder; no transaction-specific invariants are
     *  defined for `VaultWithdraw` yet.
     *
     *  @param isDelete  `true` if the entry was erased.
     *  @param before    Entry state before the transaction (nullptr if new).
     *  @param after     Entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Evaluate transaction-specific post-conditions after all entries are visited.
     *
     *  Currently a no-op placeholder that always returns `true`; no
     *  transaction-specific invariants are defined for `VaultWithdraw` yet.
     *
     *  @param tx     The transaction being applied.
     *  @param result Tentative TER result.
     *  @param fee    Fee consumed by the transaction.
     *  @param view   Read-only ledger view after the transaction.
     *  @param j      Journal for logging invariant failures.
     *  @return `true` (all invariants pass).
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
