#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class PaymentChannelFund : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit PaymentChannelFund(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
