#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `DelegateSet` transaction type.
 *
 *  Creates, updates, or deletes a `Delegate` ledger entry that grants a
 *  delegate account a named set of transaction permissions on behalf of the
 *  grantor. A single transaction covers all three operations: the action is
 *  determined by whether `sfPermissions` is non-empty and whether an existing
 *  `Delegate` SLE is present in the ledger.
 *
 *  @note Self-delegation (grantor == delegate) is rejected in `preflight`.
 *      An empty `sfPermissions` array with no existing delegate object is
 *      rejected in `preclaim` to prevent a fee-charging no-op.
 */
class DelegateSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Construct a `DelegateSet` transactor bound to the given apply context.
     *
     *  @param ctx The apply context for this transaction.
     */
    explicit DelegateSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validate the transaction structure without ledger access.
     *
     *  Rejects if `sfPermissions` exceeds `kPERMISSION_MAX_SIZE`, if the
     *  grantor and delegate are the same account, if any permission value
     *  appears more than once, or if any permission value is not delegable
     *  under the current rules.
     *
     *  @param ctx The preflight context carrying the transaction and rules.
     *  @return `tesSUCCESS` on success; a `tem*` code on structural failure.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validate ledger preconditions against a read-only view.
     *
     *  Confirms that both the grantor account and the `sfAuthorize` target
     *  account exist in the ledger. Rejects with `tecNO_ENTRY` if an empty
     *  permissions array is submitted but no existing `Delegate` SLE is found
     *  (delete-intent with no object to delete).
     *
     *  @param ctx The preclaim context carrying the transaction and a
     *      read-only ledger view.
     *  @return `tesSUCCESS`, `terNO_ACCOUNT`, `tecNO_TARGET`, or `tecNO_ENTRY`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the transaction to the mutable ledger view.
     *
     *  Dispatches across three paths based on current ledger state:
     *  - **Update**: existing `Delegate` SLE + non-empty permissions →
     *    replaces `sfPermissions` in place, no reserve change.
     *  - **Delete**: existing `Delegate` SLE + empty permissions →
     *    delegates to `deleteDelegate()`.
     *  - **Create**: no existing SLE + non-empty permissions → checks
     *    reserve, allocates a new `Delegate` SLE, inserts it into both the
     *    grantor's and the delegate's owner directories, and increments the
     *    grantor's owner count.
     *
     *  @return `tesSUCCESS` on success; `tecINSUFFICIENT_RESERVE` if the
     *      grantor cannot cover the reserve for a new object; `tecDIR_FULL`
     *      if an owner directory is full; `tecINTERNAL` if the no-SLE /
     *      empty-permissions branch is reached (defensive guard — `preclaim`
     *      should have blocked this case).
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry
     *
     *  No transaction-specific invariants are enforced; this is a no-op
     *  reserved for future use.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants
     *
     *  No transaction-specific invariants are enforced; always returns
     *  `true`. Reserved for future use.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Remove a `Delegate` SLE from the ledger and clean up its directory
     *  entries.
     *
     *  Called by `AccountDelete` to clean up delegate objects owned by an
     *  account being deleted, and by `doApply()` when the submitter passes
     *  an empty `sfPermissions` array. Removes the SLE from both the
     *  grantor's owner directory (via `sfOwnerNode`) and, if present, the
     *  delegate's owner directory (via `sfDestinationNode`), decrements the
     *  grantor's owner count, then erases the SLE.
     *
     *  @param view  The mutable ledger view to apply the deletion to.
     *  @param sle   The `Delegate` SLE to remove; must not be null.
     *  @param j     Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if a directory
     *      removal fails (indicates ledger state corruption, logged at
     *      `fatal` severity and marked unreachable in coverage).
     */
    static TER
    deleteDelegate(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);
};

}  // namespace xrpl
