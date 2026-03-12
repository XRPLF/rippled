#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class PayChanFund : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit PayChanFund(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
};

using PaymentChannelFund = PayChanFund;

}  // namespace xrpl
