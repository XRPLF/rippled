#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class EscrowCreate : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit EscrowCreate(ApplyContext& ctx) : Transactor(ctx)
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

}  // namespace xrpl
