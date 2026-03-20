#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** CLAMMDelete removes an empty CLAMM pool from the ledger.
 *  The pool must have zero active liquidity (no positions).
 *  Iterates the pseudo-account's owner directory to delete ticks,
 *  bitmaps, and trust lines (up to 512 per call).
 *  Returns tecINCOMPLETE if not all objects were deleted.
 */
class CLAMMDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit CLAMMDelete(ApplyContext& ctx) : Transactor(ctx)
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

}  // namespace xrpl
