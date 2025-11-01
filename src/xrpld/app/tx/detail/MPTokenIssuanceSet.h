#ifndef XRPL_TX_MPTOKENISSUANCESET_H_INCLUDED
#define XRPL_TX_MPTOKENISSUANCESET_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class MPTokenIssuanceSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit MPTokenIssuanceSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
