#ifndef XRPL_TX_LOANBROKERCOVERDEPOSIT_H_INCLUDED
#define XRPL_TX_LOANBROKERCOVERDEPOSIT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class LoanBrokerCoverDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit LoanBrokerCoverDeposit(ApplyContext& ctx) : Transactor(ctx)
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

//------------------------------------------------------------------------------

}  // namespace ripple

#endif
