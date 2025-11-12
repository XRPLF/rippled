#ifndef XRPL_TX_SETACCOUNT_H_INCLUDED
#define XRPL_TX_SETACCOUNT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/protocol/TxFlags.h>

namespace ripple {

class SetAccount : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit SetAccount(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

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

using AccountSet = SetAccount;

}  // namespace ripple

#endif
