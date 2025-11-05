#ifndef XRPL_TX_PAYCHAN_H_INCLUDED
#define XRPL_TX_PAYCHAN_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class PayChanCreate : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit PayChanCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using PaymentChannelCreate = PayChanCreate;

//------------------------------------------------------------------------------

class PayChanFund : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit PayChanFund(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
};

using PaymentChannelFund = PayChanFund;

//------------------------------------------------------------------------------

class PayChanClaim : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit PayChanClaim(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

using PaymentChannelClaim = PayChanClaim;

}  // namespace ripple

#endif
