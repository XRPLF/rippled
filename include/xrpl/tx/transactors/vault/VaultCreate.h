#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttVAULT_CREATE` transaction type.
 *
 *  Instantiates a new Vault on the ledger: a pooled-asset construct that holds
 *  a designated asset inside a synthetic pseudo-account and issues share tokens
 *  (via `MPTokenIssuance`) to depositors in proportion to their contribution.
 *
 *  The three-phase pipeline is:
 *  - `checkExtraFeatures` / `preflight` — amendment gating and field validation
 *    (no ledger access)
 *  - `preclaim` — read-only checks on asset validity, freeze state, and domain
 *  - `doApply` — creates the Vault SLE, its pseudo-account, the asset holding,
 *    and the share `MPTokenIssuance`; increments owner count by 2
 *
 *  @note `sfScale` is only meaningful for IOU assets; it is rejected for XRP
 *      and MPT assets in `preflight`.
 *  @note `sfDomainID` is only valid when `tfVaultPrivate` is set; it also
 *      requires the `PermissionedDomains` amendment.
 *  @see VaultDeposit, VaultWithdraw, VaultSet, VaultClawback, VaultDelete
 */
class VaultCreate : public Transactor
{
public:
    /** Standard fee and sequence consequences; does not block queued transactions. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit VaultCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on required amendments.
     *
     *  Returns `false` (producing `temDISABLED`) if `featureMPTokensV1` is not
     *  active, or if `sfDomainID` is present and `featurePermissionedDomains`
     *  is not active.  Called by `invokePreflight<VaultCreate>` before
     *  `preflight1`, so disabled transactions are rejected before any field
     *  parsing.
     *
     *  @param ctx  Preflight context.
     *  @return `true` if the required amendments are active; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the bitmask of valid flag bits for this transaction type.
     *
     *  Returns `tfVaultCreateMask`, which permits `tfVaultPrivate` and
     *  `tfVaultShareNonTransferable`.  Any flag bits outside this mask
     *  cause `preflight0` to return `temINVALID_FLAG`.
     *
     *  @param ctx  Preflight context (unused).
     *  @return Bitmask of permitted flag bits.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Perform stateless, ledger-free field validation.
     *
     *  Validates the following constraints (returning `temMALFORMED` on
     *  violation):
     *  - `sfData`, if present, must not exceed `kMAX_DATA_PAYLOAD_LENGTH`.
     *  - `sfWithdrawalPolicy`, if present, must equal
     *    `kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE` (the only supported strategy).
     *  - `sfDomainID`, if present, must be non-zero and `tfVaultPrivate` must
     *    be set (domain-scoped access applies only to private vaults).
     *  - `sfAssetsMaximum`, if present, must not be negative.
     *  - `sfMPTokenMetadata`, if present, must be non-empty and within
     *    `kMAX_MP_TOKEN_METADATA_LENGTH`.
     *  - `sfScale`, if present, is only valid for IOU assets (rejected for XRP
     *    and MPT) and must not exceed `kVAULT_MAXIMUM_IOU_SCALE`.
     *
     *  @param ctx  Preflight context carrying the transaction and active rules.
     *  @return `tesSUCCESS` or `temMALFORMED`.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Perform read-only ledger checks after signature verification.
     *
     *  Checks, in order (returning the first failing code):
     *  - `canAddHolding`: the asset can be held (e.g., MPT issuance exists).
     *  - Pseudo-account issuer rejection: if the asset's issuer is a
     *    pseudo-account (e.g., an AMM LP token or another vault's share MPT),
     *    returns `tecWRONG_ASSET`.  Such assets cannot be clawed back if needed.
     *  - Freeze check: returns `tecFROZEN` (IOU) or `tecLOCKED` (MPT) if the
     *    asset is frozen for the vault owner.
     *  - Domain existence: if `sfDomainID` is present, the referenced
     *    `PermissionedDomain` object must exist; returns `tecOBJECT_NOT_FOUND`
     *    otherwise.
     *  - Address collision: derives the pseudo-account address from the vault
     *    keylet; returns `terADDRESS_COLLISION` if the derivation yields zero.
     *
     *  @param ctx  Preclaim context carrying a read-only ledger view.
     *  @return `tesSUCCESS`, `tecWRONG_ASSET`, `tecFROZEN`, `tecLOCKED`,
     *      `tecOBJECT_NOT_FOUND`, `terADDRESS_COLLISION`, or a code from
     *      `canAddHolding`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the vault creation to the mutable ledger view.
     *
     *  Executes the following steps atomically:
     *  1. Links a new Vault SLE into the owner's directory via `dirLink`.
     *  2. Increments the owner count by 2 (vault + pseudo-account), then checks
     *     `preFeeBalance_` against the new reserve requirement; returns
     *     `tecINSUFFICIENT_RESERVE` if insufficient.  The increment-before-check
     *     order is intentional: the account must be able to afford both objects.
     *  3. Creates the pseudo-account via `createPseudoAccount` keyed from the
     *     vault's object ID with `sfVaultID` as the discriminator field.
     *  4. Adds an empty asset holding on the pseudo-account via `addEmptyHolding`
     *     (either an `MPToken` or a `TrustLine`/`RippleState`).
     *  5. Creates the share `MPTokenIssuance` on the pseudo-account at sequence 1
     *     via `MPTokenIssuanceCreate::create`.  The `tfVaultShareNonTransferable`
     *     flag suppresses `lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer`;
     *     `tfVaultPrivate` sets `lsfMPTRequireAuth`.
     *  6. Populates and inserts the Vault SLE with all fields.
     *  7. Explicitly authorizes the vault creator's `MPToken` via
     *     `authorizeMPToken`.  For private vaults, also authorizes the
     *     pseudo-account itself (so it can hold its own share tokens internally).
     *  8. Calls `associateAsset` on the vault SLE — this must be the final
     *     write to round stored numeric values to the asset's precision.
     *
     *  @return `tesSUCCESS` or a `tec*` / `ter*` error.
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op placeholder; no transaction-specific invariants are
     *  defined for `VaultCreate` yet.
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
     *  transaction-specific invariants are defined for `VaultCreate` yet.
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
