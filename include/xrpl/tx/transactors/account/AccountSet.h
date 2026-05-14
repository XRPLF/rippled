#pragma once

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Processes AccountSet transactions, the primary mechanism for configuring an
 *  account root entry on the XRP Ledger.
 *
 *  Covers behavioral flags (RequireAuth, DisableMaster, NoFreeze, GlobalFreeze,
 *  DepositAuth, disallow-incoming variants), metadata fields (Domain, EmailHash,
 *  MessageKey, WalletLocator), economic parameters (TransferRate, TickSize), and
 *  NFT settings (authorized minter).
 *
 *  The `ConsequencesFactory` is `Custom` because most AccountSet transactions are
 *  normal but those that touch `asfRequireAuth`, `asfDisableMaster`,
 *  `asfAccountTxnID`, or the legacy `tfRequireAuth`/`tfOptionalAuth` flags are
 *  classified as blockers, which prevent subsequent same-account transactions from
 *  being queued until the blocker confirms.
 *
 *  @note Flag changes accept two parallel encodings for historical compatibility:
 *      the legacy transaction-flags bitfield (`tfRequireAuth`, etc.) and the
 *      modern `sfSetFlag`/`sfClearFlag` fields (`asfRequireAuth`, etc.). Both
 *      paths are validated and applied in all three pipeline phases.
 */
class AccountSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    explicit AccountSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Determine whether this AccountSet is a normal or blocker transaction.
     *
     *  Returns `Blocker` when the transaction sets or clears `asfRequireAuth`,
     *  `asfDisableMaster`, or `asfAccountTxnID`, or when the legacy
     *  `tfRequireAuth`/`tfOptionalAuth` transaction flags are used. Blocker
     *  classification prevents later same-account transactions from being queued
     *  until this one confirms, guarding state-dependent sequences (e.g.,
     *  establishing trust lines before enabling RequireAuth).
     *
     *  @param ctx Preflight context containing the raw transaction.
     *  @return A `TxConsequences` with `Category::Blocker` or `Category::Normal`.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Return the set of transaction flags valid for AccountSet.
     *
     *  @param ctx Preflight context (unused; present for interface uniformity).
     *  @return `tfAccountSetMask`, causing any unknown flag bits to be rejected
     *      by the framework's `preflight1` call.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Perform stateless validation of AccountSet transaction fields.
     *
     *  Runs without ledger access. Validates:
     *  - No flag is simultaneously set and cleared via the dual-path flag system
     *    (legacy `Flags` bitfield and `sfSetFlag`/`sfClearFlag` are both accepted
     *    but must not contradict each other for RequireAuth, RequireDestTag, and
     *    DisallowXRP).
     *  - `sfTransferRate`, if present, is zero (unset) or within
     *    [`QUALITY_ONE`, `2 × QUALITY_ONE`]. Values below `QUALITY_ONE` are
     *    rejected to prevent discount-rate value creation.
     *  - `sfTickSize`, if present, is zero (unset) or within
     *    [`Quality::kMIN_TICK_SIZE`, `Quality::kMAX_TICK_SIZE`].
     *  - `sfMessageKey`, if present and non-empty, is a recognized public key type.
     *  - `sfDomain`, if present, does not exceed `kMAX_DOMAIN_LENGTH`.
     *  - Setting `asfAuthorizedNFTokenMinter` requires `sfNFTokenMinter` present;
     *    clearing it requires `sfNFTokenMinter` absent.
     *
     *  @param ctx Preflight context.
     *  @return `tesSUCCESS` on valid input, or a `tem*`/`tel*` code describing
     *      the first validation failure encountered.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce the granular delegation model for delegate-signed AccountSet
     *  transactions.
     *
     *  If no `sfDelegate` field is present the transaction passes immediately.
     *  Otherwise the method looks up the `DelegateObject` for `(account, delegate)`
     *  and rejects the transaction unless every modified field has been explicitly
     *  granted:
     *  - Any use of `sfSetFlag`, `sfClearFlag`, or the legacy `Flags` bitfield
     *    is categorically rejected — behavioral flag changes cannot be delegated.
     *  - `sfWalletLocator` and `sfNFTokenMinter` are unconditionally blocked.
     *  - `sfEmailHash`, `sfMessageKey`, `sfDomain`, `sfTransferRate`, and
     *    `sfTickSize` are permitted only when the corresponding granular permission
     *    constant (`AccountEmailHashSet`, `AccountMessageKeySet`, etc.) is present
     *    in the delegate's permission list.
     *
     *  @param view Read-only ledger view used to locate the `DelegateObject`.
     *  @param tx   The AccountSet transaction being evaluated.
     *  @return `tesSUCCESS` if delegation is absent or fully authorized;
     *      `terNO_DELEGATE_PERMISSION` if the delegate object is missing or any
     *      required granular permission has not been granted.
     */
    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    /** Perform read-only ledger-state checks that may still reject the transaction.
     *
     *  Two state-dependent constraints are enforced here:
     *  - **RequireAuth**: enabling `asfRequireAuth` when the account's owner
     *    directory is non-empty is rejected (`tecOWNERS` or `terOWNERS` when
     *    `tapRetry` is set), preventing retroactive breakage of existing trust
     *    relationships.
     *  - **Clawback / NoFreeze mutual exclusion** (when `featureClawback` is
     *    active): `asfAllowTrustLineClawback` cannot be set if `lsfNoFreeze` is
     *    already set, and vice versa. Enabling clawback also requires an empty
     *    owner directory for the same retroactive-breakage reason.
     *
     *  @param ctx Preclaim context providing a read-only ledger view and the
     *      transaction.
     *  @return `tesSUCCESS` on success; `tecOWNERS`, `terOWNERS`, or
     *      `tecNO_PERMISSION` when the relevant constraint is violated;
     *      `terNO_ACCOUNT` if the account root does not exist.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the AccountSet transaction, mutating the account root SLE.
     *
     *  Reads the current `sfFlags` bitmask, computes the new value by applying
     *  each flag change in sequence (both legacy-bitfield and `sfSetFlag`/
     *  `sfClearFlag` paths), updates scalar metadata fields, and calls
     *  `ctx_.view().update(sle)` once at the end.
     *
     *  Security-sensitive flag handling:
     *  - **DisableMaster**: requires the transaction to be signed with the master
     *    key itself and the account to already have an alternative signing path
     *    (`sfRegularKey` or a signer list); returns `tecNEED_MASTER_KEY` or
     *    `tecNO_ALTERNATIVE_KEY` otherwise.
     *  - **NoFreeze**: likewise requires a master-key signature when master is
     *    still enabled. `asfNoFreeze` is permanent — the flag can never be
     *    cleared.
     *  - **GlobalFreeze interlock**: once `lsfNoFreeze` is active, clearing
     *    `lsfGlobalFreeze` is prohibited to prevent selective market manipulation.
     *
     *  Scalar fields (`sfDomain`, `sfEmailHash`, `sfMessageKey`, `sfWalletLocator`,
     *  `sfTransferRate`, `sfTickSize`) use an empty or zero value as a deletion
     *  signal (`makeFieldAbsent`) rather than a separate clear flag.
     *
     *  Amendment-gated flags (`asfAllowTrustLineLocking` via `featureTokenEscrow`,
     *  `asfAllowTrustLineClawback` via `featureClawback`) are only applied when
     *  the corresponding amendment is active.
     *
     *  @return `tesSUCCESS` on success; `tecNEED_MASTER_KEY` if a master-key
     *      operation was attempted without a master-key signature;
     *      `tecNO_ALTERNATIVE_KEY` if DisableMaster was requested with no fallback
     *      signing path; `tefINTERNAL` if the account SLE cannot be located
     *      (indicates ledger corruption, expected unreachable).
     */
    TER
    doApply() override;

    /** Per-entry hook for AccountSet-specific invariant checking.
     *
     *  Currently a no-op placeholder; AccountSet has no transaction-specific
     *  invariants beyond the global set.
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

    /** Finalize AccountSet-specific invariant checks after all entries are visited.
     *
     *  Currently a no-op placeholder; always returns `true`.
     *
     *  @param tx     The AccountSet transaction.
     *  @param result The TER result from `doApply`.
     *  @param fee    The fee charged for this transaction.
     *  @param view   Read-only view of the ledger after application.
     *  @param j      Journal for diagnostic logging.
     *  @return `true` unconditionally (no AccountSet-specific invariants yet).
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
