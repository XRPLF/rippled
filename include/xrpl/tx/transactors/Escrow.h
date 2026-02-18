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

//------------------------------------------------------------------------------

class EscrowFinish : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit EscrowFinish(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    preflightSigValidated(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

class EscrowCancel : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit EscrowCancel(ApplyContext& ctx) : Transactor(ctx)
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
