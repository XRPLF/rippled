#ifndef XRPL_TX_NFTOKENCANCELOFFER_H_INCLUDED
#define XRPL_TX_NFTOKENCANCELOFFER_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class NFTokenCancelOffer : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit NFTokenCancelOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
