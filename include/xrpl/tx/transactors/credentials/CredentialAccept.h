#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the CredentialAccept transaction type.
 *
 *  Implements the subject-side acceptance step of the two-phase credential
 *  issuance lifecycle. After an issuer creates a credential directed at a
 *  subject via `CredentialCreate`, the subject must submit a `CredentialAccept`
 *  transaction to activate it. Only accepted credentials are usable in
 *  permission-gated operations.
 *
 *  On acceptance the reserve burden shifts atomically from issuer to subject:
 *  the issuer's owner count decrements by 1 and the subject's increments by 1.
 *  If the credential has already expired at apply time it is deleted and
 *  `tecEXPIRED` is returned, even though the accepting transaction itself
 *  fails.
 */
class CredentialAccept : public Transactor
{
public:
    /** Standard consequence semantics: consumes a sequence number, no blocking
     *  or custom consequence logic.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Construct a CredentialAccept transactor for the given apply context. */
    explicit CredentialAccept(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Return the set of flags permitted on this transaction type.
     *
     *  When `fixInvalidTxFlags` is active, restricts accepted flags to
     *  `tfUniversalMask`, causing any unknown flag bits to be rejected as
     *  malformed. Returns `0` (allow any flags) on pre-fix ledgers to preserve
     *  backward compatibility.
     *
     *  @param ctx Preflight context carrying the current rule set.
     *  @return Bitmask of valid non-universal flags, or `0` if unconstrained.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate transaction fields without accessing ledger state.
     *
     *  Enforces two structural invariants: `sfIssuer` must not be the zero
     *  account ID, and `sfCredentialType` must be non-empty and within
     *  `kMAX_CREDENTIAL_TYPE_LENGTH` bytes. Both violations produce `tem`
     *  codes, meaning the transaction is permanently invalid and will not be
     *  retried.
     *
     *  @param ctx Preflight context carrying the transaction and rule set.
     *  @return `tesSUCCESS` on valid input; `temINVALID_ACCOUNT_ID` if
     *      `sfIssuer` is zero; `temMALFORMED` if `sfCredentialType` is empty
     *      or too long.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Perform read-only ledger checks before applying the transaction.
     *
     *  Verifies that the issuer account exists, that the credential object
     *  keyed by `(subject, issuer, credentialType)` is present in the ledger,
     *  and that `lsfAccepted` has not already been set on it.
     *
     *  @param ctx Preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS` if all checks pass; `tecNO_ISSUER` if the issuer
     *      account does not exist; `tecNO_ENTRY` if the credential object is
     *      absent; `tecDUPLICATE` if the credential is already accepted.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the acceptance state transition against the mutable ledger view.
     *
     *  Checks that the subject has sufficient reserve to take ownership of the
     *  credential (comparing `preFeeBalance_` against the post-increment owner
     *  reserve). If the credential has expired by `parentCloseTime`, deletes it
     *  via `credentials::deleteSLE` and returns `tecEXPIRED`. On the happy
     *  path, sets `lsfAccepted` on the credential SLE and transfers the owner
     *  count: issuer decremented by 1, subject incremented by 1.
     *
     *  @return `tesSUCCESS` on successful acceptance; `tecINSUFFICIENT_RESERVE`
     *      if the subject cannot afford the incremented reserve; `tecEXPIRED`
     *      if the credential has passed its expiration (credential is deleted);
     *      `tefINTERNAL` if account SLEs or the credential SLE cannot be loaded
     *      (unreachable under correct ledger invariants).
     *  @note The reserve is checked against `preFeeBalance_` (balance before
     *      fee deduction), ensuring the fee has already been accounted for.
     */
    TER
    doApply() override;

    /** Invariant visitor hook; no credential-specific invariants are checked. */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Invariant finalizer hook; no credential-specific invariants are checked.
     *
     *  @return Always `true` (no invariants to enforce yet).
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
