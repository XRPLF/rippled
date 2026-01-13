#ifndef XRPL_TX_CANCELOFFER_H_INCLUDED
#define XRPL_TX_CANCELOFFER_H_INCLUDED

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

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

}  // namespace xrpl

#endif
