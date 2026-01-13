#ifndef XRPL_TX_VAULTDEPOSIT_H_INCLUDED
#define XRPL_TX_VAULTDEPOSIT_H_INCLUDED

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class VaultDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultDeposit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl

#endif
