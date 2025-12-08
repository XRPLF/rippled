#ifndef XRPL_TX_VAULTCLAWBACK_H_INCLUDED
#define XRPL_TX_VAULTCLAWBACK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

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

    std::pair<STAmount, STAmount>
    burnShares(
        std::shared_ptr<SLE> const& vault,
        std::shared_ptr<SLE const> const& sleShareIssuance,
        AccountID const& holder);

    Expected<std::pair<STAmount, STAmount>, TER>
    clawbackAssets(
        std::shared_ptr<SLE> const& vault,
        std::shared_ptr<SLE const> const& sleShareIssuance,
        AccountID const& holder,
        STAmount const& clawbackAmount);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
