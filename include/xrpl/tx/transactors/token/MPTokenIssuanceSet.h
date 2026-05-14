#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for `ttMPTOKEN_ISSUANCE_SET` transactions.
 *
 *  Handles two distinct operations on MPTokenIssuance ledger objects:
 *  1. Toggling the locked state of the whole issuance or an individual
 *     holder's `MPToken` slot (via `tfMPTLock` / `tfMPTUnlock`).
 *  2. Mutating post-creation properties of the issuance — `sfMutableFlags`,
 *     `sfTransferFee`, `sfMPTokenMetadata`, and `sfDomainID` — when the
 *     `featureDynamicMPT` amendment is active.
 *
 *  Uses compile-time (static) polymorphism: the three pipeline entry points
 *  (`checkExtraFeatures`, `preflight`, `preclaim`) are static methods driven
 *  by `Transactor::invokePreflight<T>` rather than virtual dispatch.
 *  `ConsequencesFactory{Normal}` applies standard fee consequences; this
 *  transaction does not unconditionally block later transactions from the
 *  same account.
 */
class MPTokenIssuanceSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit MPTokenIssuanceSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate on amendment requirements not captured by the global tx-to-feature
     *  map.
     *
     *  Returns `false` (causing `invokePreflight` to return `temDISABLED`)
     *  if `sfDomainID` is present in the transaction but either
     *  `featurePermissionedDomains` or `featureSingleAssetVault` is not yet
     *  enabled. All other transactions pass unconditionally.
     *
     *  @param ctx Preflight context providing access to transaction fields
     *      and active amendment rules.
     *  @return `true` if the transaction may proceed, `false` to abort with
     *      `temDISABLED`.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the valid flag-bit mask for this transaction type.
     *
     *  Delegates to `tfMPTokenIssuanceSetMask`, which covers `tfMPTLock` and
     *  `tfMPTUnlock`. `preflight1` uses this mask to reject transactions that
     *  set any undefined flag bits.
     *
     *  @param ctx Preflight context (unused; present for framework uniformity).
     *  @return Bitmask of all flag bits this transaction legitimately owns.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless semantic validation (no ledger access).
     *
     *  Enforces the following invariants:
     *  - `sfDomainID` and `sfHolder` are mutually exclusive.
     *  - `tfMPTLock` and `tfMPTUnlock` cannot both be set.
     *  - The submitting account cannot equal `sfHolder`.
     *
     *  When `featureDynamicMPT` is active, additionally:
     *  - Mutation fields (`sfMutableFlags`, `sfMPTokenMetadata`,
     *    `sfTransferFee`) cannot coexist with `sfHolder` or non-universal
     *    tx flags.
     *  - Setting a non-zero `sfTransferFee` while clearing `tmfMPTClearCanTransfer`
     *    is rejected.
     *  - `sfMutableFlags` must be non-zero and must not include unknown bits
     *    (validated against `tmfMPTokenIssuanceSetMutableMask`).
     *
     *  When either `featureSingleAssetVault` or `featureDynamicMPT` is
     *  enabled, a transaction that changes nothing (zero flags, no domain,
     *  no mutation fields) is rejected as `temMALFORMED`.
     *
     *  @param ctx Preflight context with the transaction and active rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code on any violation.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validate delegate authorization for lock/unlock operations.
     *
     *  If `sfDelegate` is absent the call is a no-op and returns
     *  `tesSUCCESS`. Otherwise the delegate SLE is loaded and permissions
     *  are checked in two tiers:
     *  1. Broad transaction-level permission via `checkTxPermission()`.
     *  2. Granular fall-back: `MPTokenIssuanceLock` required for `tfMPTLock`,
     *     `MPTokenIssuanceUnlock` required for `tfMPTUnlock`.
     *
     *  @param view Read-only ledger view used to load the delegate SLE.
     *  @param tx   The transaction being validated.
     *  @return `tesSUCCESS` if authorized; `terNO_DELEGATE_PERMISSION` if the
     *      delegate lacks the required permission.
     */
    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    /** Read-only ledger-state validation (runs after signature verification).
     *
     *  Checks that:
     *  - The target `MPTokenIssuance` object exists and the submitter is its
     *    issuer.
     *  - If `sfHolder` is present: the holder account exists and their
     *    `MPToken` slot for this issuance exists.
     *  - If `sfDomainID` is present: the issuance has `lsfMPTRequireAuth` set,
     *    and the referenced `PermissionedDomain` object exists (unless the
     *    zero sentinel is supplied to clear the domain link).
     *  - Lock/unlock operations respect the `lsfMPTCanLock` flag: under older
     *    rules the flag must be present; under `featureSingleAssetVault` or
     *    `featureDynamicMPT` the flag is only required when the tx actually
     *    requests a lock or unlock.
     *  - For `featureDynamicMPT` mutations: each flag the transaction tries to
     *    set or clear must be listed in `sfMutableFlags` on the ledger object
     *    (table-driven via `kMPT_MUTABILITY_FLAGS`); similarly,
     *    `sfMPTokenMetadata` and `sfTransferFee` mutations require
     *    `lsmfMPTCanMutateMetadata` and `lsmfMPTCanMutateTransferFee`
     *    respectively. Setting a non-zero `sfTransferFee` additionally
     *    requires `lsfMPTCanTransfer` to already be set on the object.
     *
     *  @param ctx Preclaim context providing read-only access to the ledger.
     *  @return `tesSUCCESS` on success; `tecOBJECT_NOT_FOUND`,
     *      `tecNO_PERMISSION`, `tecNO_AUTH`, `tecNO_ENTRY`, or another
     *      `tec*` / `ter*` code on failure.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the transaction's mutations to the ledger.
     *
     *  Peeks at the `MPTokenIssuance` SLE (or the holder's `MPToken` SLE
     *  when `sfHolder` is present) and applies the following changes as
     *  directed by the transaction flags and fields:
     *  - `tfMPTLock` / `tfMPTUnlock`: sets or clears `lsfMPTLocked`.
     *  - `sfMutableFlags`: iterates `kMPT_MUTABILITY_FLAGS` and sets/clears
     *    each `canMutateFlag` on the issuance as directed.
     *  - When `tmfMPTClearCanTransfer` is applied, `sfTransferFee` is
     *    explicitly removed to keep the ledger internally consistent.
     *  - `sfTransferFee`: stored absent when zero (field uses `soeDEFAULT`
     *    semantics), present otherwise.
     *  - `sfMPTokenMetadata`: an empty blob removes the field entirely.
     *  - `sfDomainID`: zero sentinel removes the field; any other value sets
     *    it.
     *
     *  All failure paths should have been caught in preflight/preclaim; if
     *  `doApply` reaches an unexpected state it returns `tecINTERNAL`.
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` on an unexpected error.
     */
    TER
    doApply() override;

    /** Per-entry invariant hook (currently a no-op).
     *
     *  Called by the invariant-checker framework for each SLE modified by
     *  this transaction. No transaction-specific invariants are registered
     *  yet; this override exists as a placeholder for future work.
     *
     *  @param isDelete `true` if the entry was deleted.
     *  @param before   SLE state before the transaction (may be null for new
     *      entries).
     *  @param after    SLE state after the transaction (may be null for
     *      deletions).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-apply invariant finalizer (currently a no-op).
     *
     *  Called once after all `visitInvariantEntry` calls for this
     *  transaction. No transaction-specific invariants are registered yet;
     *  always returns `true`. This override exists as a placeholder for
     *  future work.
     *
     *  @param tx     The applied transaction.
     *  @param result The `TER` result of `doApply`.
     *  @param fee    Fee charged for this transaction.
     *  @param view   Read-only view of the ledger after application.
     *  @param j      Journal for diagnostic logging.
     *  @return `true` (invariant trivially satisfied).
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
