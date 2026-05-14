#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `DepositPreauth` transaction type.
 *
 *  Manages the whitelist of accounts and credential-bearing parties permitted
 *  to send payments to an account that has enabled `lsfDepositAuth`. Without a
 *  preauthorization entry, any inbound payment to such an account fails.
 *
 *  Supports four mutually exclusive operations, selected by which single field
 *  is present in the transaction: `sfAuthorize`, `sfUnauthorize`,
 *  `sfAuthorizeCredentials`, and `sfUnauthorizeCredentials`. The credential
 *  variants are gated on the `featureCredentials` amendment.
 *
 *  @note The `ConsequencesFactory` is `Normal` — this transaction does not
 *      block other transactions from the same account in the fee queue.
 */
class DepositPreauth : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit DepositPreauth(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate credential-based operations on the `featureCredentials` amendment.
     *
     *  Returns `false` (causing `invokePreflight` to emit `temDISABLED`) when
     *  `sfAuthorizeCredentials` or `sfUnauthorizeCredentials` is present but
     *  the `featureCredentials` amendment is not yet active on the ledger.
     *
     *  @param ctx Preflight context carrying the transaction and active rules.
     *  @return `true` if the transaction is permitted to proceed to `preflight`;
     *      `false` if a required amendment is disabled.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of field presence and basic semantic correctness.
     *
     *  Enforces that exactly one of `sfAuthorize`, `sfUnauthorize`,
     *  `sfAuthorizeCredentials`, or `sfUnauthorizeCredentials` is present,
     *  returning `temMALFORMED` otherwise. For account-based operations,
     *  validates that the target `AccountID` is non-zero and that the
     *  authorizing account is not attempting to preauthorize itself
     *  (`temCANNOT_PREAUTH_SELF`). For credential-based operations, delegates
     *  array validation to `credentials::checkArray`.
     *
     *  @param ctx Preflight context; no ledger view is available.
     *  @return `tesSUCCESS` on valid input, or a `tem*` code describing the
     *      specific malformation.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger checks for authorization and de-authorization.
     *
     *  For `sfAuthorize`: verifies the target account exists (`tecNO_TARGET`)
     *  and that no duplicate `DepositPreauth` entry already exists for the pair
     *  (`tecDUPLICATE`). For `sfUnauthorize`: verifies the entry being removed
     *  is present (`tecNO_ENTRY`). Credential variants additionally verify that
     *  every credential issuer is a live account (`tecNO_ISSUER`), and sort the
     *  credential set canonically before computing the ledger key to ensure the
     *  duplicate check is order-independent.
     *
     *  @param ctx Preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS`, or a `tec*`/`tef*` code if a ledger-state
     *      constraint is violated.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the preauthorization change to the ledger.
     *
     *  For authorization operations (`sfAuthorize` / `sfAuthorizeCredentials`),
     *  checks the owner reserve against `preFeeBalance_` (the balance before
     *  fee deduction), creates a `DepositPreauth` SLE, inserts it into the
     *  ledger and the account's owner directory, and increments the owner count.
     *  For de-authorization, delegates to `removeFromLedger`. Credentials are
     *  re-sorted before being written to the SLE to preserve canonical order.
     *
     *  @return `tesSUCCESS` on success, `tecINSUFFICIENT_RESERVE` if the
     *      account lacks sufficient reserve to own another entry, or a `tef*`
     *      code on internal ledger inconsistency.
     *  @note Reserve is checked against `preFeeBalance_` so that an account
     *      may spend its base reserve on fees while still being rejected for
     *      creating a new owned object it cannot afford.
     */
    TER
    doApply() override;

    /** No-op invariant visitor; transaction-specific invariants not yet defined. */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op invariant finalizer; transaction-specific invariants not yet defined.
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

    /** Remove a `DepositPreauth` SLE from the ledger by its index.
     *
     *  Locates the entry, removes it from the owner's directory using the
     *  `sfOwnerNode` page cached in the SLE, decrements the owner count, and
     *  erases the entry. Called both from `doApply` (for `sfUnauthorize` /
     *  `sfUnauthorizeCredentials`) and from `AccountDelete` when cleaning up
     *  all owned objects before deleting an account.
     *
     *  @param view  Mutable ledger view on which to operate.
     *  @param delIndex Ledger key of the `DepositPreauth` entry to remove.
     *  @param j     Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success; `tecNO_ENTRY` if the SLE is missing;
     *      `tefBAD_LEDGER` if the owner-directory removal fails (indicates
     *      ledger corruption — should be unreachable under normal operation).
     */
    static TER
    removeFromLedger(ApplyView& view, uint256 const& delIndex, beast::Journal j);
};

}  // namespace xrpl
