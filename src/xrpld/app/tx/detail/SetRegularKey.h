#ifndef XRPL_TX_SETREGULARKEY_H_INCLUDED
#define XRPL_TX_SETREGULARKEY_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class SetRegularKey : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit SetRegularKey(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
