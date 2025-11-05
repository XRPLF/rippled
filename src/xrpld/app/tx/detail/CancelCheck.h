#ifndef XRPL_TX_CANCELCHECK_H_INCLUDED
#define XRPL_TX_CANCELCHECK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class CancelCheck : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit CancelCheck(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using CheckCancel = CancelCheck;

}  // namespace ripple

#endif
