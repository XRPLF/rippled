#ifndef XRPL_TX_DEPOSIT_PREAUTH_H_INCLUDED
#define XRPL_TX_DEPOSIT_PREAUTH_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class DepositPreauth : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DepositPreauth(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    // Interface used by DeleteAccount
    static TER
    removeFromLedger(
        ApplyView& view,
        uint256 const& delIndex,
        beast::Journal j);
};

}  // namespace ripple

#endif
