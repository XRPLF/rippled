#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class Clawback : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit Clawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
