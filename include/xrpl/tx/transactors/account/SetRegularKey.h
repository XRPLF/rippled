#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class SetRegularKey : public Transactor
{
public:
    virtual ~SetRegularKey() = default;
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit SetRegularKey(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    TER
    doApply() override;
};

}  // namespace xrpl
