#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for creating or updating a LoanBroker ledger object (XLS-66).
 *
 *  Processes `ttLOAN_BROKER_SET` transactions. A `LoanBroker` intermediates
 *  between a `Vault` (the liquidity pool) and individual borrowers, holding
 *  collateral in an associated pseudo-account and earning a management fee
 *  from loan interest. This transactor handles both initial creation and
 *  subsequent configuration updates via the same "set" (upsert) convention
 *  used throughout the ledger: creation is implicit when `sfLoanBrokerID` is
 *  absent; update is triggered when it is present.
 *
 *  Only the vault owner may create or modify a broker associated with their
 *  vault. Rate fields (`sfManagementFeeRate`, `sfCoverRateMinimum`,
 *  `sfCoverRateLiquidation`) are immutable after creation — they cannot be
 *  renegotiated once loans may be outstanding. On creation, the owner count
 *  is incremented by two: one for the `LoanBroker` SLE and one for its
 *  pseudo-account.
 *
 *  @see LoanBrokerDelete, LoanBrokerCoverDeposit, LoanBrokerCoverWithdraw,
 *      LoanSet
 */
class LoanBrokerSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanBrokerSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gates the transaction on the lending protocol amendment.
     *
     *  Delegates to `checkLendingProtocolDependencies`. Amendment checks
     *  belong here, not in `preflight`.
     *
     *  @param ctx Preflight context providing ledger rules.
     *  @return `true` if all lending protocol amendments are enabled;
     *      `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of transaction fields.
     *
     *  Validates the following without ledger access:
     *  - `sfData`, if present, must not exceed `maxDataPayloadLength`.
     *  - `sfManagementFeeRate`, `sfCoverRateMinimum`, `sfCoverRateLiquidation`:
     *    each independently checked against its protocol maximum via
     *    `validNumericRange`; absent fields pass.
     *  - `sfDebtMaximum`, if present, must lie in `[0, maxMPTokenAmount]`.
     *  - In update mode (`sfLoanBrokerID` present): `sfManagementFeeRate`,
     *    `sfCoverRateMinimum`, and `sfCoverRateLiquidation` are rejected —
     *    these rate fields are immutable after creation.
     *  - `sfLoanBrokerID` and `sfVaultID`, if present, must be non-zero.
     *  - Cover-rate pairing: `sfCoverRateMinimum` and `sfCoverRateLiquidation`
     *    must either both be zero or both be non-zero; a one-sided threshold
     *    is incoherent.
     *
     *  @param ctx Preflight context carrying the transaction fields.
     *  @return `temINVALID` if any field fails validation; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Returns the set of optional numeric fields that must round-trip cleanly
     *  through the vault's asset representation.
     *
     *  Currently contains only `~sfDebtMaximum`. `preclaim` iterates this
     *  list and rejects any value where `STAmount{asset, *value} != *value`,
     *  catching precision loss for non-IOU assets (XRP integer drops, MPTokens).
     *  The vector is static to avoid repeated heap allocation.
     *
     *  @return A reference to the static vector of optional `STNumber` fields
     *      subject to asset-precision checks.
     */
    static std::vector<OptionaledField<STNumber>> const&
    getValueFields();

    /** Validates the transaction against current ledger state (read-only).
     *
     *  Checks in order:
     *  1. The referenced vault (`sfVaultID`) exists (`tecNO_ENTRY`).
     *  2. The sender (`sfAccount`) is the vault's `sfOwner` (`tecNO_PERMISSION`).
     *  3. **Update mode** (`sfLoanBrokerID` present):
     *     a. The broker object exists (`tecNO_ENTRY`).
     *     b. The broker's `sfVaultID` matches the transaction's `sfVaultID` —
     *        vault association is immutable (`tecNO_PERMISSION`).
     *     c. The sender owns the broker (`tecNO_PERMISSION`).
     *     d. If `sfDebtMaximum` is set to a non-zero value below the broker's
     *        current `sfDebtTotal`, the update is rejected (`tecLIMIT_EXCEEDED`)
     *        — this would strand outstanding loans above the new limit.
     *  4. **Creation mode** (`sfLoanBrokerID` absent):
     *     a. `canAddHolding` confirms the vault can accept a new asset holding.
     *     b. `checkFrozen` ensures the vault's pseudo-account is not frozen
     *        for the vault asset.
     *  5. All fields in `getValueFields()` are verified to round-trip cleanly
     *     through `STAmount{asset, *value}` (`tecPRECISION_LOSS` on mismatch).
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return A `TER` indicating the first failing check, or `tesSUCCESS`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Applies the create or update to the mutable ledger view.
     *
     *  **Update path** (`sfLoanBrokerID` present): writes `sfData` and
     *  `sfDebtMaximum` to the existing broker SLE (rate fields are
     *  intentionally omitted, enforcing immutability at apply time), then
     *  calls `view.update()` and `associateAsset`.
     *
     *  **Create path** (`sfLoanBrokerID` absent):
     *  1. Derives the broker keylet from `(account_, sequence)`.
     *  2. Creates two directory links via `dirLink`: one in the human owner's
     *     account directory, and one in the vault pseudo-account's directory
     *     (`sfVaultNode`) so the vault can enumerate its brokers.
     *  3. Increments `sfOwnerCount` by **two** — one for the `LoanBroker`
     *     SLE, one for its pseudo-account — then checks `preFeeBalance_`
     *     against the updated reserve requirement.
     *  4. Creates the broker pseudo-account via `createPseudoAccount`, keyed
     *     on the broker's ledger key with `sfLoanBrokerID` as the back-reference.
     *  5. Calls `addEmptyHolding` to establish the broker's asset position on
     *     the pseudo-account.
     *  6. Initializes all SLE fields: `sfSequence`, `sfVaultID`, `sfOwner`,
     *     `sfAccount`, `sfLoanSequence` (starts at 1), and any optional fields
     *     present in the transaction. `sfLoanSequence` is consumed by `LoanSet`
     *     to index loans issued through this broker.
     *  7. Inserts the broker SLE and calls `associateAsset`.
     *
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if a required SLE is
     *      unexpectedly absent (indicates ledger corruption — these branches
     *      are `LCOV_EXCL` because preclaim guards against them);
     *      `tecINTERNAL` if the vault disappears between preclaim and doApply;
     *      `tecINSUFFICIENT_RESERVE` if the owner's pre-fee balance cannot
     *      cover the two-unit reserve increase.
     */
    TER
    doApply() override;

    /** Per-entry hook for transaction-specific invariant checks.
     *
     *  No transaction-specific invariants are currently registered; this
     *  override is a placeholder for future work.
     *
     *  @param isDelete `true` if the entry is being deleted.
     *  @param before SLE state before the transaction, or `nullptr` for new entries.
     *  @param after SLE state after the transaction, or `nullptr` for deleted entries.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalizes transaction-specific invariant checks.
     *
     *  No transaction-specific invariants are currently registered; always
     *  returns `true`. Placeholder for future work.
     *
     *  @param tx The transaction being applied.
     *  @param result The `TER` result from `doApply`.
     *  @param fee The XRP fee charged for the transaction.
     *  @param view Read-only view of the ledger after application.
     *  @param j Journal for diagnostic logging.
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

//------------------------------------------------------------------------------

}  // namespace xrpl
