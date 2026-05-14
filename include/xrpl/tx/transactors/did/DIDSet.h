#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for `ttDID_SET` transactions.
 *
 *  Creates or updates a Decentralized Identifier (DID) ledger object owned
 *  by the submitting account, conforming to the W3C DID v1.0 specification
 *  (https://www.w3.org/TR/did-core/). Each account may hold at most one DID
 *  object; submitting a `DIDSet` transaction when a DID already exists
 *  performs a field-level upsert rather than replacement.
 *
 *  @note Gated behind the `featureDID` amendment. The sibling transactor
 *      `DIDDelete` handles removal.
 */
class DIDSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit DIDSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validate the transaction fields before any ledger state is touched.
     *
     *  Enforces three rules without accessing the ledger:
     *  - At least one of `sfURI`, `sfDIDDocument`, or `sfData` must be present.
     *  - Not all three fields may be simultaneously present but empty (which
     *    would create or leave a semantically vacuous DID object).
     *  - No individual field may exceed its maximum byte length
     *    (`kMAX_DIDURI_LENGTH`, `kMAX_DID_DOCUMENT_LENGTH`, `kMAX_DID_DATA_LENGTH`).
     *
     *  @param ctx  Preflight context carrying the raw transaction fields.
     *  @return `temEMPTY_DID` if no non-empty field is provided; `temMALFORMED`
     *      if any field exceeds its length limit; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Apply the DID create-or-update to the ledger.
     *
     *  Follows an upsert pattern keyed on `keylet::did(account_)`:
     *
     *  - **Update path** (DID SLE already exists): for each of `sfURI`,
     *    `sfDIDDocument`, and `sfData`, an absent transaction field is a
     *    no-op, an empty field removes the attribute from the existing object,
     *    and a non-empty field replaces the stored value. Returns
     *    `tecEMPTY_DID` if all three fields would be absent after the update.
     *
     *  - **Create path** (no DID SLE exists): builds a fresh SLE, copies
     *    non-empty transaction fields into it, then calls the file-local
     *    `addSLE()` helper, which verifies owner reserve availability, inserts
     *    the object into the ledger, links it into the account's owner
     *    directory, and increments the owner count.
     *
     *  @return `tesSUCCESS` on success; `tecEMPTY_DID` if the update would
     *      leave the DID with no attributes; `tecINSUFFICIENT_RESERVE` if the
     *      account cannot cover the new owner reserve (create path only);
     *      `tecDIR_FULL` if the owner directory has no room (create path only).
     */
    TER
    doApply() override;

    /** Invariant visitor — no DID-specific invariants are currently enforced. */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Invariant finalizer — no DID-specific invariants are currently enforced.
     *
     *  @return `true` always.
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
