#pragma once

#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class LoanSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType kConsequencesFactory{Normal};

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

    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

public:
    static std::uint32_t constexpr kMinPaymentTotal = 1;
    static std::uint32_t constexpr kDefaultPaymentTotal = 1;
    static_assert(kDefaultPaymentTotal >= kMinPaymentTotal);

    static std::uint32_t constexpr kMinPaymentInterval = 60;
    static std::uint32_t constexpr kDefaultPaymentInterval = 60;
    static_assert(kDefaultPaymentInterval >= kMinPaymentInterval);

    static std::uint32_t constexpr kDefaultGracePeriod = 60;
    static_assert(kDefaultGracePeriod >= kMinPaymentInterval);
};

//------------------------------------------------------------------------------

}  // namespace xrpl
