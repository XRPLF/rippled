#ifndef XRPL_TX_CONFIDENTIALCLAWSBACK_H_INCLUDED
#define XRPL_TX_CONFIDENTIALCLAWSBACK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

class ConfidentialMPTClawback : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl

#endif
