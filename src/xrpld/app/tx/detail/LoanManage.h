#ifndef XRPL_TX_LOANMANAGE_H_INCLUDED
#define XRPL_TX_LOANMANAGE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class LoanManage : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit LoanManage(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Helper function that might be needed by other transactors
     */
    static TER
    defaultLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref brokerSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /** Helper function that might be needed by other transactors
     */
    static TER
    impairLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref vaultSle,
        beast::Journal j);

    /** Helper function that might be needed by other transactors
     */
    static TER
    unimpairLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref vaultSle,
        beast::Journal j);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

}  // namespace ripple

#endif
