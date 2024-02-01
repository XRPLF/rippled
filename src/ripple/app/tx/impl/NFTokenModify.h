

#ifndef RIPPLE_TX_NFTOKENMODIFY_H_INCLUDED
#define RIPPLE_TX_NFTOKENMODIFY_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

class NFTokenModify : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit NFTokenModify(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
