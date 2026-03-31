#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class LoanBrokerCoverClawback : public Transactor
{
public:
    virtual ~LoanBrokerCoverClawback() = default;
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit LoanBrokerCoverClawback(ApplyContext& ctx) : Transactor(ctx)
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
