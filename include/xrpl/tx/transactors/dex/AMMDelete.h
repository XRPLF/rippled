#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Removes a fully-drained AMM pool and its associated ledger objects.
 *
 *  When all liquidity has been withdrawn via `AMMWithdraw` and
 *  `sfLPTokenBalance` on the `ltAMM` entry reaches zero, the pool's
 *  supporting objects (trustlines, MPToken entries, the `ltAMM` record, and
 *  the AMM pseudo-account `AccountRoot`) are not cleaned up automatically.
 *  `AMMDelete` handles that deferred cleanup.
 *
 *  Because a long-lived pool may accumulate hundreds of LP-token trustlines,
 *  deletion is chunked: each invocation removes at most
 *  `maxDeletableAMMTrustLines` (512) trustlines and commits that partial
 *  progress to the ledger. If trustlines remain, `doApply` returns
 *  `tecINCOMPLETE` and the submitter must re-submit until the pool is fully
 *  removed.
 *
 *  @note Deletion is only permitted when `sfLPTokenBalance` is exactly zero.
 *      Use `AMMWithdraw` to drain a pool that still holds liquidity.
 *  @see AMMCreate, AMMWithdraw
 */
class AMMDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on required feature flags.
     *
     *  Returns false when the base AMM feature is not yet active, or when
     *  either pool asset is an MPT issue and `featureMPTokensV2` has not
     *  been enabled. The MPT gate prevents deletion of MPT-based pools on
     *  network versions that do not fully support MPT cleanup.
     *
     *  @param ctx  The preflight context providing the current rules and tx.
     *  @return true if all required amendments are active; false otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Validate the transaction fields without ledger access.
     *
     *  All amendment checks are handled by `checkExtraFeatures`; there are
     *  no field-level constraints that can be verified without consulting
     *  ledger state. Always returns `tesSUCCESS`.
     *
     *  @param ctx  The preflight context containing the transaction.
     *  @return `tesSUCCESS` unconditionally.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Verify the AMM pool exists and has been fully drained.
     *
     *  Reads the `ltAMM` object for the submitted asset pair and checks that
     *  `sfLPTokenBalance` is exactly zero. Returns `terNO_AMM` if no such
     *  pool exists, or `tecAMM_NOT_EMPTY` if LP tokens are still outstanding.
     *  Both failure codes prevent fee collection; use `AMMWithdraw` to drain
     *  a pool before deleting it.
     *
     *  @param ctx  The preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the pool exists and is empty; `terNO_AMM` if
     *      the asset pair has no AMM; `tecAMM_NOT_EMPTY` if LP tokens remain.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Delete AMM trustlines and, when all are gone, the pool itself.
     *
     *  Delegates to `deleteAMMAccount()` inside an isolated `Sandbox` view.
     *  That helper deletes up to `maxDeletableAMMTrustLines` (512) trustlines
     *  per call. The sandbox is applied to the live ledger on both
     *  `tesSUCCESS` and `tecINCOMPLETE`, committing partial progress even
     *  when the deletion is not yet complete. When `tesSUCCESS` is returned,
     *  any remaining MPToken entries, the `ltAMM` object, and the AMM
     *  pseudo-account `AccountRoot` are also erased.
     *
     *  @return `tesSUCCESS` when the pool and all its objects are fully
     *      removed; `tecINCOMPLETE` when trustlines remain and the caller
     *      must re-submit.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry
     *
     *  Currently a no-op for `AMMDelete`; reserved for future
     *  transaction-specific invariants.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants
     *
     *  Currently returns true unconditionally for `AMMDelete`; reserved for
     *  future transaction-specific post-conditions.
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
