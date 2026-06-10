#pragma once

#include <xrpl/tx/Transactor.h>

#include <expected>

namespace xrpl {

class VaultClawback : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit VaultClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

private:
    std::expected<std::pair<STAmount, STAmount>, TER>
    assetsToClawback(
        SLE::ref vault,
        SLE::const_ref sleShareIssuance,
        AccountID const& holder,
        STAmount const& clawbackAmount);
};

}  // namespace xrpl
