#ifndef XRPL_TX_CONFIDENTIALMERGEINBOX_H_INCLUDED
#define XRPL_TX_CONFIDENTIALMERGEINBOX_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

class ConfidentialMergeInbox : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMergeInbox(ApplyContext& ctx) : Transactor(ctx)
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
