#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class CheckCancel : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit CheckCancel(ApplyContext& ctx) : Transactor(ctx)
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
