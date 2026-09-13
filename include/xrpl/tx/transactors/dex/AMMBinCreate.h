#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** AMMBinCreate provisions a single bin in a CtBinned AMM pool.
 *
 *  Splits bin lifecycle out of AMMDeposit so that the per-bin
 *  MPTokenIssuance creation happens in a transactor that's properly
 *  privileged for it (`CreateMptIssuance`). AMMDeposit then only ever
 *  authorizes + mints — never creates issuances — and fits cleanly
 *  under `MayAuthorizeMpt`.
 *
 *  Idempotent semantics: if the bin already exists, returns
 *  `tecAMM_FAILED`. Caller is expected to check (or accept that price)
 *  before submitting.
 *
 *  Tx fields:
 *    sfAccount      — submitter (anyone can create bins; they're
 *                     immediately usable by all LPs)
 *    sfAsset, sfAsset2  — the binned pool's asset pair
 *    sfBinID        — which bin to create
 */
class AMMBinCreate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit AMMBinCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

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
