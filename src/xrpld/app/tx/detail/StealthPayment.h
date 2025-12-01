#ifndef RIPPLE_TX_STEALTH_PAYMENT_H_INCLUDED
#define RIPPLE_TX_STEALTH_PAYMENT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class StealthPayment : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit StealthPayment(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
