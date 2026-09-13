#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** AMMBinDestroy decommissions an empty bin in a CtBinned AMM pool.
 *
 *  Counterpart to AMMBinCreate. Removes the bin SLE and its per-bin
 *  MPTokenIssuance once the bin has been fully drained (zero reserves,
 *  zero MPT outstanding). Anyone can call — this is pure cleanup.
 *
 *  Failure modes:
 *    bin does not exist                 → tecNO_ENTRY
 *    bin still has reserves / shares    → tecAMM_FAILED
 *    bin is the AMM's active bin        → tecAMM_FAILED
 *                                         (re-deposit elsewhere first;
 *                                         AMMWithdraw advances activeBinID
 *                                         when it drains the active bin)
 *
 *  Tx fields:
 *    sfAccount, sfAsset, sfAsset2, sfBinID
 */
class AMMBinDestroy : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit AMMBinDestroy(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
