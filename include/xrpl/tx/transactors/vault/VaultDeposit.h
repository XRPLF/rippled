#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttVAULT_DEPOSIT` transaction type.
 *
 *  Deposits assets into a Vault ledger object and mints vault-share MPTokens
 *  for the depositor in exchange — the primary liquidity-provision operation
 *  for on-ledger asset pools.
 *
 *  The three-phase pipeline is:
 *  - `preflight` — rejects a zero `sfVaultID` and any non-positive `sfAmount`
 *    without accessing the ledger
 *  - `preclaim` — read-only checks: vault existence, asset type match, freeze
 *    state, and — for private vaults — domain-credential authorization
 *  - `doApply` — mints shares via a two-step exchange calculation, updates
 *    `sfAssetsTotal`/`sfAssetsAvailable`, enforces the vault asset cap, and
 *    issues two `accountSend` calls (assets in, shares out), both with
 *    `WaiveTransferFee::Yes` to preserve exchange-rate accuracy
 *
 *  @note Transfer fees are intentionally waived on both legs. Allowing them
 *      would corrupt the `sfAssetsTotal` accounting and break the share
 *      exchange rate for all depositors.
 *  @note Private vault access is governed by `DomainID` credentials on the
 *      share MPT issuance, not by standard MPT issuer authorization, because
 *      the share issuer is a pseudo-account that cannot proactively authorize
 *      holders.
 *  @see VaultWithdraw, VaultCreate, VaultHelpers.h
 */
class VaultDeposit : public Transactor
{
public:
    /** Standard fee and sequence consequences; does not block queued transactions. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit VaultDeposit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Perform stateless, ledger-free field validation.
     *
     *  Checks:
     *  - `sfVaultID` must be non-zero; returns `temMALFORMED` otherwise.
     *  - `sfAmount` must be positive; returns `temBAD_AMOUNT` otherwise.
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
     *  - Asset match: `sfAmount` must carry the same asset type as the vault's
     *    `sfAsset` field; returns `tecWRONG_ASSET` otherwise.
     *  - Transferability: the vault asset must be transferable from the
     *    depositor to the vault pseudo-account; returns the result of
     *    `canTransfer` otherwise.
     *  - Freeze / lock: returns `tecFROZEN` (IOU) or `tecLOCKED` (MPT) if the
     *    deposited asset is frozen for the depositor, and `tecLOCKED` if the
     *    vault's share MPT is locked for the depositor.
     *  - Private vault authorization: if `lsfVaultPrivate` is set and the
     *    depositor is not the vault owner, the vault's share MPT issuance must
     *    carry a `sfDomainID`; `credentials::validDomain` must succeed (or
     *    return only `tecEXPIRED`, which is suppressed here so that `doApply`
     *    can delete the expired credentials as a side effect); returns
     *    `tecNO_AUTH` if no `sfDomainID` is present.
     *  - Sufficient balance: the depositor must hold at least `sfAmount` of the
     *    vault asset; returns `tecINSUFFICIENT_FUNDS` otherwise.
     *
     *  @param ctx  Preclaim context carrying a read-only ledger view.
     *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecWRONG_ASSET`, `tecFROZEN`,
     *      `tecLOCKED`, `tecNO_AUTH`, `tecINSUFFICIENT_FUNDS`, or a code from
     *      `canTransfer` / `requireAuth`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the vault deposit to the mutable ledger view.
     *
     *  Executes the following steps atomically:
     *  1. Ensures the depositor holds (or creates) an MPToken for vault shares
     *     via `enforceMPTokenAuthorization` (private vault, non-owner) or
     *     `authorizeMPToken` (public vault or vault owner). For the vault owner
     *     of a private vault, also authorizes the pseudo-account itself.
     *  2. Computes `sharesCreated` via `assetsToSharesDeposit()` (truncated to
     *     integral MPT units); returns `tecPRECISION_LOSS` if the result rounds
     *     to zero, preventing dust deposits.
     *  3. Back-computes the exact asset cost via `sharesToAssetsDeposit()` to
     *     guarantee `assetsDeposited <= sfAmount`; any sub-share-unit remainder
     *     is effectively returned to the depositor.
     *  4. Updates `sfAssetsTotal` and `sfAssetsAvailable` on the vault SLE,
     *     then enforces the vault's `sfAssetsMaximum` cap; returns
     *     `tecLIMIT_EXCEEDED` if the cap would be breached.
     *  5. Transfers `assetsDeposited` from the depositor to the vault
     *     pseudo-account via `accountSend` with `WaiveTransferFee::Yes`.
     *  6. Transfers `sharesCreated` from the vault pseudo-account to the
     *     depositor via `accountSend` with `WaiveTransferFee::Yes`.
     *  7. Calls `associateAsset` on the vault SLE — this must be the final
     *     write to round stored numeric values to the asset's precision.
     *
     *  If `assetsToSharesDeposit` or `sharesToAssetsDeposit` throws
     *  `std::overflow_error` (e.g., due to a large scale factor combined with
     *  large balances), the exception is caught and `tecPATH_DRY` is returned.
     *
     *  @return `tesSUCCESS`, `tecPRECISION_LOSS`, `tecLIMIT_EXCEEDED`,
     *      `tecPATH_DRY`, or a `tec*` / `ter*` code from the underlying helpers.
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op placeholder; no transaction-specific invariants are
     *  defined for `VaultDeposit` yet.
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
     *  transaction-specific invariants are defined for `VaultDeposit` yet.
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
