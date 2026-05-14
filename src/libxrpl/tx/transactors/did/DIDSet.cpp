/** @file
 *  Implementation of the `DIDSet` transactor, which creates or updates a
 *  Decentralized Identifier (DID) ledger object (`ltDID`) owned by the
 *  submitting account. The DID object stores up to three independently-optional
 *  blob fields — `sfURI`, `sfDIDDocument`, and `sfData` — each capped at 256
 *  bytes. The implementation conforms to the W3C DID v1.0 specification
 *  (https://www.w3.org/TR/did-core/).
 *
 *  @note The symmetric removal path lives in `DIDDelete.cpp`. The file-local
 *      `addSLE` helper mirrors `DIDDelete::deleteSLE` in reverse: reserve
 *      check → insert → dirInsert → adjustOwnerCount(+1).
 */
#include <xrpl/tx/transactors/did/DIDSet.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <memory>

namespace xrpl {

/** Validate DID transaction fields before any ledger state is read.
 *
 *  Enforces two emptiness invariants:
 *  1. At least one of `sfURI`, `sfDIDDocument`, or `sfData` must be present in
 *     the transaction. A transaction with none of them is meaningless.
 *  2. All three fields may not be simultaneously present but empty. A client
 *     sending `URI=""`, `DIDDocument=""`, `Data=""` is attempting to wipe an
 *     existing DID via `DIDSet` rather than `DIDDelete`; the protocol blocks
 *     this by treating "all-present, all-empty" as equivalent to no payload.
 *
 *  Also enforces per-field byte-length limits via the `isTooLong` lambda, which
 *  uses the `~sField` (optional) accessor so absent fields are silently skipped.
 *
 *  @param ctx  Preflight context containing the raw transaction fields.
 *  @return `temEMPTY_DID` if no non-empty field payload is provided;
 *      `temMALFORMED` if any present field exceeds its maximum byte length
 *      (`kMAX_DIDURI_LENGTH`, `kMAX_DID_DOCUMENT_LENGTH`, or
 *      `kMAX_DID_DATA_LENGTH`); `tesSUCCESS` otherwise.
 */
NotTEC
DIDSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.tx.isFieldPresent(sfURI) && !ctx.tx.isFieldPresent(sfDIDDocument) &&
        !ctx.tx.isFieldPresent(sfData))
        return temEMPTY_DID;

    if (ctx.tx.isFieldPresent(sfURI) && ctx.tx[sfURI].empty() &&
        ctx.tx.isFieldPresent(sfDIDDocument) && ctx.tx[sfDIDDocument].empty() &&
        ctx.tx.isFieldPresent(sfData) && ctx.tx[sfData].empty())
        return temEMPTY_DID;

    auto isTooLong = [&](auto const& sField, std::size_t length) -> bool {
        if (auto field = ctx.tx[~sField])
            return field->length() > length;
        return false;
    };

    if (isTooLong(sfURI, kMAX_DIDURI_LENGTH) ||
        isTooLong(sfDIDDocument, kMAX_DID_DOCUMENT_LENGTH) ||
        isTooLong(sfData, kMAX_DID_DATA_LENGTH))
        return temMALFORMED;

    return tesSUCCESS;
}

/** Insert a new owner-tracked SLE into the ledger with all required bookkeeping.
 *
 *  Performs the standard three-step sequence for creating any owned ledger
 *  object: reserve check, object insertion, and directory linkage.
 *
 *  1. **Reserve check**: verifies `sfBalance ≥ accountReserve(ownerCount + 1)`
 *     before touching anything. The check uses the pre-insertion owner count so
 *     that `tecINSUFFICIENT_RESERVE` is returned cleanly with no partial state.
 *  2. **Insertion**: calls `ctx.view().insert(sle)` to add the object to the
 *     ledger view.
 *  3. **Directory linkage**: calls `dirInsert` to register the object's key in
 *     the account's owner directory and stores the returned page index in
 *     `sfOwnerNode` on the SLE. That cached index enables O(1) removal by
 *     `DIDDelete::deleteSLE` via `dirRemove`. Increments `sfOwnerCount` on the
 *     account root and writes the updated account SLE back.
 *
 *  @param ctx    Apply context providing the mutable ledger view and journal.
 *  @param sle    The freshly constructed SLE to insert; must not yet be present
 *      in the view.
 *  @param owner  Account that will own the new object.
 *  @return `tecINSUFFICIENT_RESERVE` if the account balance is below the new
 *      reserve threshold; `tecDIR_FULL` if the owner directory has no room
 *      (unreachable in practice — marked `LCOV_EXCL_LINE`); `tesSUCCESS` on
 *      success.
 */
static TER
addSLE(ApplyContext& ctx, std::shared_ptr<SLE> const& sle, AccountID const& owner)
{
    auto const sleAccount = ctx.view().peek(keylet::account(owner));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    {
        auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
        auto const reserve = ctx.view().fees().accountReserve((*sleAccount)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    ctx.view().insert(sle);

    {
        auto page =
            ctx.view().dirInsert(keylet::ownerDir(owner), sle->key(), describeOwnerDir(owner));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*sle)[sfOwnerNode] = *page;
    }
    adjustOwnerCount(ctx.view(), sleAccount, 1, ctx.journal);
    ctx.view().update(sleAccount);

    return tesSUCCESS;
}

/** Apply the DID create-or-update to the ledger.
 *
 *  Dispatches to one of two paths based on whether a DID SLE already exists
 *  for the submitting account (keyed by `keylet::did(account_)`).
 *
 *  **Update path** (SLE exists): applies a local `update` lambda to each of
 *  `sfURI`, `sfDIDDocument`, and `sfData`. The lambda is intentionally
 *  asymmetric: an absent transaction field is a no-op (leaves the stored value
 *  unchanged), an empty field actively removes the attribute from the SLE via
 *  `makeFieldAbsent` (surgical clear), and a non-empty field overwrites the
 *  stored value. After all three updates, the code re-checks whether all fields
 *  are now absent — an update that clears the last remaining attribute produces
 *  an empty DID, which is rejected with `tecEMPTY_DID`. Note the `tec` code
 *  rather than `tem`: the fee is still charged because preflight already passed.
 *
 *  **Create path** (no SLE exists): constructs a fresh `ltDID` SLE, sets
 *  `sfAccount`, and copies non-empty transaction fields via a `set` lambda
 *  (absent or empty fields are skipped). Under `fixEmptyDID`, if all three
 *  fields end up absent in the new SLE the function returns `tecEMPTY_DID`
 *  before inserting. Without that amendment, the create path allowed an SLE
 *  where every payload field was absent (because `preflight` only caught the
 *  "all-present and all-empty" case, not the "all-present but some non-empty
 *  strings that happen to be zero-length" edge). Finally, `addSLE` is called
 *  to validate the owner reserve, insert the SLE, link it into the owner
 *  directory, and increment `sfOwnerCount`.
 *
 *  @return `tesSUCCESS` on success; `tecEMPTY_DID` if the resulting DID would
 *      carry no attributes; `tecINSUFFICIENT_RESERVE` if the account cannot
 *      cover the incremental owner reserve (create path only); `tecDIR_FULL`
 *      if the owner directory cannot accommodate the new entry (create path
 *      only, unreachable in practice).
 */
TER
DIDSet::doApply()
{
    Keylet const didKeylet = keylet::did(account_);
    if (auto const sleDID = ctx_.view().peek(didKeylet))
    {
        // Update path: absent field = no-op, empty = remove, non-empty = replace.
        auto update = [&](auto const& sField) {
            if (auto const field = ctx_.tx[~sField])
            {
                if (field->empty())
                {
                    sleDID->makeFieldAbsent(sField);
                }
                else
                {
                    (*sleDID)[sField] = *field;
                }
            }
        };
        update(sfURI);
        update(sfDIDDocument);
        update(sfData);

        if (!sleDID->isFieldPresent(sfURI) && !sleDID->isFieldPresent(sfDIDDocument) &&
            !sleDID->isFieldPresent(sfData))
        {
            return tecEMPTY_DID;
        }
        ctx_.view().update(sleDID);
        return tesSUCCESS;
    }

    // Create path: build a fresh SLE and populate non-empty fields only.
    auto const sleDID = std::make_shared<SLE>(didKeylet);
    (*sleDID)[sfAccount] = account_;

    auto set = [&](auto const& sField) {
        if (auto const field = ctx_.tx[~sField]; field && !field->empty())
            (*sleDID)[sField] = *field;
    };

    set(sfURI);
    set(sfDIDDocument);
    set(sfData);

    // fixEmptyDID closes the preflight gap: reject creation of an SLE where
    // every payload field ended up absent (e.g. all values were empty strings
    // that passed the "not all three present-and-empty" preflight check).
    if (ctx_.view().rules().enabled(fixEmptyDID) && !sleDID->isFieldPresent(sfURI) &&
        !sleDID->isFieldPresent(sfDIDDocument) && !sleDID->isFieldPresent(sfData))
    {
        return tecEMPTY_DID;
    }

    return addSLE(ctx_, sleDID, account_);
}

/** Invariant visitor stub — no DID-specific per-entry invariants are enforced.
 *
 *  The global invariant framework (`ValidAMM`, `XRPNotCreated`, etc.) already
 *  covers the properties that matter for DID objects. This override exists only
 *  to satisfy the `Transactor` interface contract.
 */
void
DIDSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

/** Invariant finalizer stub — no DID-specific post-transaction invariants are
 *  enforced.
 *
 *  @return `true` always.
 */
bool
DIDSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
