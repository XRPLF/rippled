/** @file
 *  Declares the LedgerStateFix transactor for the `ttLEDGER_STATE_FIX`
 *  transaction type, a protocol-level maintenance mechanism that corrects
 *  corrupted or inconsistent ledger state.
 *
 *  Gated on the `fixNFTokenPageLinks` amendment. New repair operations may
 *  be introduced in future amendments by extending the `FixType` enum.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Repairs corrupted ledger state via a protocol-level maintenance transaction.
 *
 *  `LedgerStateFix` implements `ttLEDGER_STATE_FIX`, which allows network
 *  participants to correct inconsistent ledger entries. The specific repair
 *  operation is selected by the `sfLedgerFixType` field; currently the only
 *  supported type is `NfTokenPageLink`, which heals broken forward/backward
 *  links in an account's NFToken page chain.
 *
 *  The fee is one owner reserve (same as `AccountDelete`) to deter frivolous
 *  submissions. The fee is non-refundable even when no repair is needed.
 *
 *  @note Amendment gating is handled by the framework before `preflight` is
 *      called; no manual amendment check is required inside the lifecycle
 *      methods.
 */
class LedgerStateFix : public Transactor
{
public:
    /** Identifies which ledger-repair operation the transaction performs.
     *
     *  The `sfLedgerFixType` field in the transaction carries one of these
     *  values as a `uint16_t`. All three lifecycle methods dispatch on this
     *  enum; unknown values are rejected at `preflight` with
     *  `tefINVALID_LEDGER_FIX_TYPE`.
     */
    enum class FixType : std::uint16_t {
        /** Repair broken forward/backward links between NFToken page SLEs
         *  in an account's NFToken directory.
         *
         *  Requires `sfOwner` to be present in the transaction.
         */
        NfTokenPageLink = 1,
    };

    /** Not a queue blocker; later transactions from the same account may
     *  proceed without waiting for this one to be applied.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LedgerStateFix(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validate the transaction's static fields.
     *
     *  Checks that `sfLedgerFixType` maps to a known `FixType`. For
     *  `NfTokenPageLink`, also verifies that `sfOwner` is present.
     *
     *  @param ctx  The preflight context; no ledger access is available here.
     *  @return `tesSUCCESS` on success; `tefINVALID_LEDGER_FIX_TYPE` if the
     *      fix type is unrecognised; `temINVALID` if a required field is
     *      absent for the selected fix type.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Compute the fee for this transaction.
     *
     *  Returns one owner reserve, matching the pricing strategy used by
     *  `AccountDelete`. The elevated fee serves as an economic deterrent
     *  against frivolous repair submissions.
     *
     *  @param view  The current read-only ledger view.
     *  @param tx    The transaction being evaluated.
     *  @return The fee in drops (one owner reserve).
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Verify ledger preconditions before applying the repair.
     *
     *  For `NfTokenPageLink`, confirms that the account identified by
     *  `sfOwner` exists in the ledger. This check is deferred to preclaim
     *  (rather than preflight) because ledger state may change between
     *  submission and application.
     *
     *  @param ctx  The preclaim context, providing read-only ledger access.
     *  @return `tesSUCCESS` if preconditions are satisfied;
     *      `tecOBJECT_NOT_FOUND` if the owner account does not exist;
     *      `tecINTERNAL` (unreachable in practice) if an unknown fix type
     *      reaches this stage despite passing preflight.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the ledger repair operation.
     *
     *  For `NfTokenPageLink`, calls `nft::repairNFTokenDirectoryLinks` to
     *  walk the owner's NFToken page chain and correct any broken links.
     *
     *  @return `tesSUCCESS` if the repair completed; `tecFAILED_PROCESSING`
     *      if the repair helper could not make progress; `tecINTERNAL`
     *      (unreachable in practice) if an unknown fix type reaches this
     *      stage despite passing preflight.
     */
    TER
    doApply() override;

    /** No-op: no transaction-specific invariants are currently defined.
     *
     *  Reserved for future use. Accumulates nothing.
     *
     *  @param isDelete  True if the entry was erased.
     *  @param before    The SLE state before the transaction.
     *  @param after     The SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op: no transaction-specific invariants are currently defined.
     *
     *  Reserved for future use. Always returns `true`.
     *
     *  @param tx      The transaction that was applied.
     *  @param result  The tentative TER from `doApply`.
     *  @param fee     The fee consumed.
     *  @param view    The post-apply ledger view.
     *  @param j       Journal for diagnostics.
     *  @return Always `true` (no invariants to check yet).
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
