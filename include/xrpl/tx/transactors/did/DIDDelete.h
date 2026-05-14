#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for `ttDID_DELETE` (type 50) transactions.
 *
 *  Removes a Decentralized Identifier (DID) ledger object that was previously
 *  created by `DIDSet`. Gated behind the `featureDID` amendment. Each account
 *  may hold at most one DID object, so the target is derived entirely from the
 *  submitting account's `AccountID` — no additional fields are required on the
 *  transaction.
 *
 *  The `deleteSLE` overload pair is designed for reuse: `AccountDelete` calls
 *  the `ApplyView`-level overload directly to clean up an owned DID without
 *  constructing a full transactor context.
 */
class DIDDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit DIDDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the transaction before any ledger state is touched.
     *
     *  Returns `tesSUCCESS` unconditionally. A DID deletion carries no
     *  transaction-specific fields beyond the universal base fields; all
     *  meaningful validation (account existence, fee, sequence, signature,
     *  amendment gate) is handled by the `invokePreflight` machinery.
     *
     *  @param ctx  The preflight context (unused beyond base validation).
     *  @return `tesSUCCESS` always.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Convenience overload: peek the DID object by keylet and delegate deletion.
     *
     *  Resolves `sleKeylet` through `ctx.view()`. If the object does not exist,
     *  returns `tecNO_ENTRY` (fee-claiming failure — sequence consumed, no
     *  mutation applied). Otherwise delegates to the `ApplyView` overload.
     *
     *  @param ctx        The apply context providing the mutable ledger view and journal.
     *  @param sleKeylet  Keylet identifying the DID SLE to remove.
     *  @param owner      `AccountID` of the account that owns the DID.
     *  @return `tesSUCCESS` on successful removal; `tecNO_ENTRY` if the DID
     *      object does not exist; propagates any error from the `ApplyView`
     *      overload.
     */
    static TER
    deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner);

    /** Remove a DID SLE from the ledger and update the owner's accounting.
     *
     *  Performs the three-step deletion sequence required for any owned ledger
     *  object:
     *  1. `dirRemove` — removes the entry from the owner's directory using the
     *     cached `sfOwnerNode` page index. Returns `tefBAD_LEDGER` on failure
     *     (ledger corruption; `LCOV_EXCL` branch, should never occur).
     *  2. `adjustOwnerCount(..., -1)` — decrements the reserve-adjusted owner
     *     count on the account root. Returns `tecINTERNAL` if the account SLE
     *     cannot be found (`LCOV_EXCL` branch).
     *  3. `view.erase(sle)` — removes the DID SLE from the ledger.
     *
     *  Accepting `ApplyView&` rather than `ApplyContext&` makes this overload
     *  callable from any transactor with a mutable view — notably `AccountDelete`,
     *  which invokes it directly to clean up owned DIDs during account removal.
     *
     *  @param view   Mutable ledger view to apply changes to.
     *  @param sle    Pre-resolved shared pointer to the DID SLE to delete.
     *  @param owner  `AccountID` of the account that owns the DID.
     *  @param j      Journal for diagnostic logging.
     *  @return `tesSUCCESS` on successful removal; `tefBAD_LEDGER` if
     *      `dirRemove` fails (ledger inconsistency); `tecINTERNAL` if the
     *      owner account SLE cannot be found.
     */
    static TER
    deleteSLE(ApplyView& view, std::shared_ptr<SLE> sle, AccountID const owner, beast::Journal j);

    /** Apply the DID deletion to the ledger.
     *
     *  Derives the DID keylet from `account_` via `keylet::did` and delegates
     *  to `deleteSLE(ctx_, keylet, account_)`. Since each account holds at most
     *  one DID, no field lookup is needed.
     *
     *  @return `tesSUCCESS` on success; `tecNO_ENTRY` if the account has no DID;
     *      or a `tef`/`tec` error from `deleteSLE`.
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
