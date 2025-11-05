#ifndef XRPL_TX_CHANGE_H_INCLUDED
#define XRPL_TX_CHANGE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class Change : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit Change(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    TER
    doApply() override;
    void
    preCompute() override;

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx)
    {
        return XRPAmount{0};
    }

    static TER
    preclaim(PreclaimContext const& ctx);

private:
    TER
    applyAmendment();

    TER
    applyFee();

    TER
    applyUNLModify();
};

using EnableAmendment = Change;
using SetFee = Change;
using UNLModify = Change;

}  // namespace ripple

#endif
