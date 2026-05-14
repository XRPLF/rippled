#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the CredentialDelete transaction type.
 *
 *  Removes a `Credential` ledger object — the on-chain record that an issuer
 *  has asserted a verifiable claim about a subject — from the ledger state.
 *  Either the subject or the issuer may delete a credential unconditionally.
 *  A third party may only delete a credential that has already expired, which
 *  allows orphaned objects to be pruned by anyone and their reserve reclaimed.
 *
 *  @see CredentialCreate, CredentialAccept
 */
class CredentialDelete : public Transactor
{
public:
    /** Standard consequence semantics: consumes a sequence number, no blocking
     *  or custom consequence logic.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Construct a CredentialDelete transactor for the given apply context. */
    explicit CredentialDelete(ApplyContext& ctx) : Transactor(ctx)
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
     *  Enforces three structural invariants: at least one of `sfSubject` or
     *  `sfIssuer` must be present (both absent → `temMALFORMED`, as the
     *  credential cannot be identified); any present `sfSubject` or `sfIssuer`
     *  must be a non-zero `AccountID`; and `sfCredentialType` must be non-empty
     *  and within `kMAX_CREDENTIAL_TYPE_LENGTH` bytes.
     *
     *  @param ctx Preflight context carrying the transaction and rule set.
     *  @return `tesSUCCESS` on valid input; `temMALFORMED` if both identity
     *      fields are absent or `sfCredentialType` is empty or too long;
     *      `temINVALID_ACCOUNT_ID` if `sfSubject` or `sfIssuer` is the zero
     *      account.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Perform read-only ledger checks before applying the transaction.
     *
     *  Derives the credential keylet from `(subject, issuer, credentialType)`,
     *  substituting `sfAccount` for any absent `sfSubject` or `sfIssuer`. This
     *  default-to-sender substitution is how self-deletion works: a subject can
     *  omit `sfIssuer` when they are also the sender, and vice-versa for the
     *  issuer. Returns `tecNO_ENTRY` if no matching credential exists.
     *
     *  @param ctx Preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS` if the credential exists; `tecNO_ENTRY` otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Remove the credential SLE from the ledger.
     *
     *  Applies the same sender-as-default substitution for absent `sfSubject`
     *  or `sfIssuer` as `preclaim`. Authorization is then checked: if the
     *  sender is neither the subject nor the issuer, deletion is only permitted
     *  when the credential has expired (as determined by comparing `sfExpiration`
     *  against `parentCloseTime`). A valid, unexpired credential may not be
     *  deleted by a third party. Authorized deletions are delegated to
     *  `credentials::deleteSLE()`, which unlinks the object from the owner
     *  directory and decrements the owner's reserve count.
     *
     *  @return `tesSUCCESS` on successful deletion; `tecNO_PERMISSION` if the
     *      sender is a third party and the credential has not yet expired;
     *      `tefINTERNAL` if the SLE cannot be loaded after passing `preclaim`
     *      (indicates ledger corruption, unreachable in practice).
     *  @note Expiration is evaluated against `parentCloseTime`, consistent with
     *      XRPL's convention for time-sensitive ledger operations.
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
