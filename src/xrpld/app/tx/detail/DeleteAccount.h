#ifndef XRPL_TX_DELETEACCOUNT_H_INCLUDED
#define XRPL_TX_DELETEACCOUNT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class DeleteAccount : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit DeleteAccount(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using AccountDelete = DeleteAccount;

}  // namespace ripple

#endif
