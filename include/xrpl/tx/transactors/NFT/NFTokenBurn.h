#ifndef XRPL_TX_BURNNFT_H_INCLUDED
#define XRPL_TX_BURNNFT_H_INCLUDED

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class NFTokenBurn : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit NFTokenBurn(ApplyContext& ctx) : Transactor(ctx)
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
