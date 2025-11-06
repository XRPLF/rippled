#ifndef XRPL_TX_AMMDELETE_H_INCLUDED
#define XRPL_TX_AMMDELETE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/** AMMDelete implements AMM delete transactor. This is a mechanism to
 * delete AMM in an empty state when the number of LP tokens is 0.
 * AMMDelete deletes the trustlines up to configured maximum. If all
 * trustlines are deleted then AMM ltAMM and root account are deleted.
 * Otherwise AMMDelete should be called again.
 */
class AMMDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif  // XRPL_TX_AMMDELETE_H_INCLUDED
