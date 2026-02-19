#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class VaultClawback : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

private:
    Expected<std::pair<STAmount, STAmount>, TER>
    assetsToClawback(
        std::shared_ptr<SLE> const& vault,
        std::shared_ptr<SLE const> const& sleShareIssuance,
        AccountID const& holder,
        STAmount const& clawbackAmount);
};

}  // namespace xrpl
