#ifndef XRPL_TX_VAULTDELETE_H_INCLUDED
#define XRPL_TX_VAULTDELETE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class VaultDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
