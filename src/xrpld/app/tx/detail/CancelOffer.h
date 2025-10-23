#ifndef XRPL_TX_CANCELOFFER_H_INCLUDED
#define XRPL_TX_CANCELOFFER_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/protocol/TxFlags.h>

namespace ripple {

class CancelOffer : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit CancelOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using OfferCancel = CancelOffer;

}  // namespace ripple

#endif
