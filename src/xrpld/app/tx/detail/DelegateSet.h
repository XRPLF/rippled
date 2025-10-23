#ifndef XRPL_TX_DELEGATESET_H_INCLUDED
#define XRPL_TX_DELEGATESET_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class DelegateSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DelegateSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    // Interface used by DeleteAccount
    static TER
    deleteDelegate(
        ApplyView& view,
        std::shared_ptr<SLE> const& sle,
        AccountID const& account,
        beast::Journal j);
};

}  // namespace ripple

#endif
