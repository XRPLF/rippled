#ifndef XRPL_TX_CREATECHECK_H_INCLUDED
#define XRPL_TX_CREATECHECK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class CreateCheck : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit CreateCheck(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using CheckCreate = CreateCheck;

}  // namespace ripple

#endif
