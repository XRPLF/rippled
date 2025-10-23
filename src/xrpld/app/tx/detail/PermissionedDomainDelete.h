#ifndef XRPL_TX_PERMISSIONEDDOMAINDELETE_H_INCLUDED
#define XRPL_TX_PERMISSIONEDDOMAINDELETE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

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

}  // namespace ripple

#endif
