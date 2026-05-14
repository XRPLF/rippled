#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `OracleDelete` transaction type (XLS-47d).
 *
 *  Removes a Price Oracle object from the ledger. Price Oracles bridge the
 *  blockchain and external world by publishing off-chain asset prices on-chain
 *  for consumption by decentralised applications.
 *
 *  This transactor pairs with `OracleSet`, which handles creation and updates.
 *  Deletion needs no field validation — all meaningful checks are deferred to
 *  `preclaim`. The static `deleteOracle` helper exposes the core mutation logic
 *  so that `AccountDelete` can clean up oracles during account removal without
 *  constructing a full transactor instance.
 */
class OracleDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit OracleDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless preflight validation.
     *
     *  A delete request carries only an `sfOracleDocumentID`; there is nothing
     *  to validate without ledger state, so this always returns `tesSUCCESS`.
     *  All substantive checks occur in `preclaim`.
     *
     *  @param ctx Preflight context (no ledger access).
     *  @return `tesSUCCESS` unconditionally.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation.
     *
     *  Confirms that the oracle identified by `sfOracleDocumentID` exists at
     *  `keylet::oracle(account, documentId)` and that its `sfOwner` field
     *  matches the submitting account. Because the keylet embeds the account,
     *  an ownership mismatch is structurally unreachable through normal flow
     *  (the branch is `LCOV_EXCL`).
     *
     *  @param ctx Preclaim context (read-only ledger access).
     *  @return `tesSUCCESS` if the oracle exists and is owned by the submitter;
     *      `tecNO_ENTRY` if the oracle does not exist;
     *      `terNO_ACCOUNT` if the submitting account is missing (unreachable
     *          under normal conditions — account existence is enforced earlier).
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the oracle deletion to the mutable ledger view.
     *
     *  Peeks the oracle SLE and delegates immediately to `deleteOracle`.
     *  Returns `tecINTERNAL` if the SLE cannot be found (unreachable under
     *  normal conditions — `preclaim` already confirmed existence).
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` if the oracle SLE is
     *      unexpectedly missing.
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor (no-op; reserved for future use). */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant check (no-op; reserved for future use).
     *
     *  @return `true` unconditionally.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Remove a Price Oracle SLE from the ledger and adjust owner reserves.
     *
     *  Exposed as a static helper so that `AccountDelete` (and any other
     *  transactor that must sweep owned objects) can invoke oracle deletion
     *  without constructing an `OracleDelete` transactor instance — the same
     *  pattern as `Transactor::ticketDelete`.
     *
     *  Owner-reserve adjustment follows XLS-47d: oracles with more than five
     *  `sfPriceDataSeries` entries occupy **two** reserve slots when created,
     *  so `-2` is passed to `adjustOwnerCount` for those; all others use `-1`.
     *  This threshold must stay in sync with the creation logic in `OracleSet`.
     *
     *  Deletion order: `dirRemove` from the owner directory → `adjustOwnerCount`
     *  → `view.erase`. Erasing before `dirRemove` would lose the cached page
     *  index stored in `sfOwnerNode`.
     *
     *  @param view   Mutable apply view.
     *  @param sle    The oracle SLE to delete; must not be null.
     *  @param account Account that owns the oracle.
     *  @param j      Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success;
     *      `tefBAD_LEDGER` if removal from the owner directory fails (signals
     *          ledger corruption — `LCOV_EXCL`);
     *      `tecINTERNAL` if `sle` is null or the owner `AccountRoot` is missing
     *          (`LCOV_EXCL`).
     */
    static TER
    deleteOracle(
        ApplyView& view,
        std::shared_ptr<SLE> const& sle,
        AccountID const& account,
        beast::Journal j);
};

}  // namespace xrpl
