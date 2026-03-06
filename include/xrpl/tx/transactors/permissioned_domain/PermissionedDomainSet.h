#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class PermissionedDomainSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit PermissionedDomainSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to create the Permissioned Domain. */
    TER
    doApply() override;
};

}  // namespace xrpl
