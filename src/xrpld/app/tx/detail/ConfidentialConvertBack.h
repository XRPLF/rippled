#ifndef XRPL_TX_CONFIDENTIALCONVERTBACK_H_INCLUDED
#define XRPL_TX_CONFIDENTIALCONVERTBACK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

class ConfidentialConvertBack : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialConvertBack(ApplyContext& ctx) : Transactor(ctx)
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
