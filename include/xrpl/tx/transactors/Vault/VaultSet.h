#ifndef XRPL_TX_VAULTSET_H_INCLUDED
#define XRPL_TX_VAULTSET_H_INCLUDED

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class VaultSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultSet(ApplyContext& ctx) : Transactor(ctx)
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
};

}  // namespace xrpl

#endif
