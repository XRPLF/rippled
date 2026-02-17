#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

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

}  // namespace xrpl
