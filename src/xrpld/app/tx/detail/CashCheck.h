#ifndef XRPL_TX_CASHCHECK_H_INCLUDED
#define XRPL_TX_CASHCHECK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class CashCheck : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit CashCheck(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using CheckCash = CashCheck;

}  // namespace ripple

#endif
