#ifndef XRPL_TX_LOANSET_H_INCLUDED
#define XRPL_TX_LOANSET_H_INCLUDED

#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class LoanSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit LoanSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static std::vector<OptionaledField<STNumber>> const&
    getValueFields();

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

public:
    static std::uint32_t constexpr minPaymentTotal = 1;
    static std::uint32_t constexpr defaultPaymentTotal = 1;
    static_assert(defaultPaymentTotal >= minPaymentTotal);

    static std::uint32_t constexpr minPaymentInterval = 60;
    static std::uint32_t constexpr defaultPaymentInterval = 60;
    static_assert(defaultPaymentInterval >= minPaymentInterval);

    static std::uint32_t constexpr defaultGracePeriod = 60;
    static_assert(defaultGracePeriod >= minPaymentInterval);
};

//------------------------------------------------------------------------------

}  // namespace ripple

#endif
