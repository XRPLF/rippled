#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class PermissionedDomainDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit PermissionedDomainDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to delete the Permissioned Domain. */
    TER
    doApply() override;
};

}  // namespace xrpl
