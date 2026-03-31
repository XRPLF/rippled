#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class VaultDelete : public Transactor
{
public:
    virtual ~VaultDelete() = default;
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultDelete(ApplyContext& ctx) : Transactor(ctx)
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
