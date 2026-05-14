#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the CredentialCreate transaction type.
 *
 *  Implements the issuance step of the W3C Verifiable Credentials (VC)
 *  lifecycle on the XRP Ledger. A trusted issuer attests facts about a subject
 *  account by submitting this transaction; the resulting credential SLE can be
 *  inspected by third parties without contacting the issuer again.
 *
 *  The issuer bears the reserve cost for the credential until the subject
 *  accepts it via `CredentialAccept`. For self-issued credentials (subject ==
 *  issuer) the credential is immediately marked `lsfAccepted` and inserted into
 *  only the single shared owner directory.
 *
 *  @see CredentialAccept, CredentialDelete
 */
class CredentialCreate : public Transactor
{
public:
    /** Standard consequence semantics: consumes a sequence number, no blocking
     *  or custom consequence logic.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Construct a CredentialCreate transactor for the given apply context. */
    explicit CredentialCreate(ApplyContext& ctx) : Transactor(ctx)
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
     *  Checks three structural invariants: `sfSubject` must be present and
     *  non-zero; the optional `sfURI`, if provided, must be non-empty and
     *  within `kMAX_CREDENTIAL_URI_LENGTH` bytes; and `sfCredentialType` must
     *  be non-empty and within `kMAX_CREDENTIAL_TYPE_LENGTH` bytes. All
     *  violations produce `temMALFORMED`, permanently rejecting the
     *  transaction without charging a fee.
     *
     *  @param ctx Preflight context carrying the transaction and rule set.
     *  @return `tesSUCCESS` on valid input; `temMALFORMED` if `sfSubject` is
     *      absent, `sfURI` is empty or too long, or `sfCredentialType` is empty
     *      or too long.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Perform read-only ledger checks before applying the transaction.
     *
     *  Verifies that the target subject account exists in the ledger and that
     *  no credential keyed by `(subject, issuer, credentialType)` already
     *  exists. These checks require ledger lookups and are therefore separated
     *  from `preflight`, which is cacheable across ledger closes.
     *
     *  @param ctx Preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS` if all checks pass; `tecNO_TARGET` if the subject
     *      account does not exist; `tecDUPLICATE` if the credential triple is
     *      already present in the ledger.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Create the credential SLE and insert it into the ledger.
     *
     *  Performs three sequential checks before mutating ledger state:
     *  (1) if `sfExpiration` is present and already behind the ledger's
     *  `parentCloseTime`, returns `tecEXPIRED` without creating the object;
     *  (2) confirms the issuer holds sufficient reserve for one additional owned
     *  object, comparing against `preFeeBalance_`; (3) inserts the credential
     *  into the issuer's owner directory and increments the issuer's owner count.
     *
     *  For third-party credentials (subject != issuer) the object is also
     *  inserted into the subject's owner directory (without incrementing the
     *  subject's owner count), enabling `CredentialAccept` to later transfer
     *  reserve ownership. For self-issued credentials the `lsfAccepted` flag is
     *  set immediately and only the single shared directory is used.
     *
     *  @return `tesSUCCESS` on successful creation; `tecEXPIRED` if the
     *      supplied expiration is already in the past; `tecINSUFFICIENT_RESERVE`
     *      if the issuer cannot afford the incremented reserve; `tecDIR_FULL`
     *      if either owner directory has no space for a new entry;
     *      `tefINTERNAL` if the issuer's `AccountRoot` SLE cannot be loaded
     *      (unreachable under correct ledger invariants).
     *  @note Expiration is compared against `parentCloseTime`, not the current
     *      ledger's own close time, consistent with XRPL's convention for
     *      time-sensitive operations.
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
