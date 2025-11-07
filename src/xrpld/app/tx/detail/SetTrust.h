#ifndef XRPL_TX_SETTRUST_H_INCLUDED
#define XRPL_TX_SETTRUST_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/protocol/TxFlags.h>

namespace ripple {

class SetTrust : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit SetTrust(ApplyContext& ctx) : Transactor(ctx)
    {
    }

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

using TrustSet = SetTrust;

}  // namespace ripple

#endif
