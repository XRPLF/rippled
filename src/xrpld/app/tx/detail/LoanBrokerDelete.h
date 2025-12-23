#ifndef XRPL_TX_LOANBROKERDELETE_H_INCLUDED
#define XRPL_TX_LOANBROKERDELETE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

class LoanBrokerDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit LoanBrokerDelete(ApplyContext& ctx) : Transactor(ctx)
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

}  // namespace xrpl

#endif
