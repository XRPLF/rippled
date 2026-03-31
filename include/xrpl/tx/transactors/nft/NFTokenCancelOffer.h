#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class NFTokenCancelOffer : public Transactor
{
public:
    virtual ~NFTokenCancelOffer() = default;
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit NFTokenCancelOffer(ApplyContext& ctx) : Transactor(ctx)
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
