#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class DIDSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DIDSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
