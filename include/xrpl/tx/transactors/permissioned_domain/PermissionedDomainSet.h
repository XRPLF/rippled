#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttPERMISSIONED_DOMAIN_SET` transaction.
 *
 *  Handles both creation of a new `PermissionedDomain` ledger object and
 *  in-place modification of an existing one. The presence of `sfDomainID` in
 *  the transaction selects the update path; its absence triggers creation.
 *
 *  Permissioned Domains define a named set of accepted credential types
 *  (issuer + credential-type pairs). Other protocol features can scope access
 *  to holders of those credentials without enumerating individual accounts.
 *
 *  Gated on the `featureCredentials` amendment via `checkExtraFeatures`.
 */
class PermissionedDomainSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit PermissionedDomainSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate this transaction type on the `featureCredentials` amendment.
     *
     *  Called by `invokePreflight` before `preflight`. Returns `false` (and
     *  causes `temDISABLED`) when the amendment is not yet active, preventing
     *  domain creation or modification on pre-amendment ledgers.
     *
     *  @param ctx  The preflight context carrying the current rule set.
     *  @return `true` if `featureCredentials` is enabled; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of transaction fields.
     *
     *  Delegates to `credentials::checkArray` to enforce that
     *  `sfAcceptedCredentials` is non-empty, within
     *  `kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE` (10), and
     *  structurally well-formed. Also rejects a zero-valued `sfDomainID` —
     *  a zero hash is not a valid domain identifier.
     *
     *  No ledger state is accessed here; all checks are purely against the
     *  transaction's own fields.
     *
     *  @param ctx  The preflight context.
     *  @return `tesSUCCESS` on success; `temMALFORMED` for a zero
     *      `sfDomainID`; a `tem` code from `checkArray` for invalid
     *      credential structure.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger state checks.
     *
     *  Verifies:
     *  - The submitting account exists on ledger (guard against internal
     *    inconsistency; returns `tefINTERNAL` if missing).
     *  - Every `sfIssuer` in `sfAcceptedCredentials` has a live `AccountRoot`
     *    on ledger; a domain referencing a non-existent issuer would be
     *    permanently unresolvable (`tecNO_ISSUER`).
     *  - For update operations (`sfDomainID` present): the domain SLE exists
     *    (`tecNO_ENTRY`) and is owned by the submitting account
     *    (`tecNO_PERMISSION`).
     *
     *  @param ctx  The preclaim context carrying the read-only ledger view.
     *  @return `tesSUCCESS`, `tefINTERNAL`, `tecNO_ISSUER`, `tecNO_ENTRY`,
     *      or `tecNO_PERMISSION`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the transaction, creating or updating a PermissionedDomain SLE.
     *
     *  Canonicalises `sfAcceptedCredentials` via `credentials::makeSorted`
     *  before writing, ensuring deterministic storage order regardless of
     *  submission order.
     *
     *  **Update path** (`sfDomainID` present): replaces the existing domain
     *  SLE's credential array in-place; no reserve change occurs.
     *
     *  **Create path** (`sfDomainID` absent): checks that the account's XRP
     *  balance covers `accountReserve(ownerCount + 1)`, allocates a new SLE
     *  keyed by `keylet::permissionedDomain(account_, sfSequence)`, inserts it
     *  into the owner directory, and increments the owner count by one.
     *
     *  The transaction sequence number is used as the domain's key
     *  differentiator, making each domain globally unique without a separate
     *  ID-generation step.
     *
     *  @return `tesSUCCESS`; `tecINSUFFICIENT_RESERVE` if the account cannot
     *      cover the new reserve; `tecDIR_FULL` if the owner directory has no
     *      space; `tefINTERNAL` (unreachable in normal operation) if the owner
     *      or domain SLE is unexpectedly absent.
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor — currently a no-op (future work).
     *
     *  @param isDelete  True when the entry is being deleted.
     *  @param before    The SLE state before the transaction.
     *  @param after     The SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant finalization — currently a no-op (future work).
     *
     *  @param tx     The applied transaction.
     *  @param result The TER code returned by `doApply`.
     *  @param fee    The fee charged.
     *  @param view   The post-transaction read view.
     *  @param j      Journal for diagnostic logging.
     *  @return Always `true`; no transaction-specific invariants are checked yet.
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
