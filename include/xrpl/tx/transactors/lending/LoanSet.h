#pragma once

#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class LoanSet : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

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
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

public:
    static constexpr std::uint32_t kMinPaymentTotal = 1;
    static constexpr std::uint32_t kDefaultPaymentTotal = 1;
    static_assert(kDefaultPaymentTotal >= kMinPaymentTotal);

    static constexpr std::uint32_t kMinPaymentInterval = 60;
    static constexpr std::uint32_t kDefaultPaymentInterval = 60;
    static_assert(kDefaultPaymentInterval >= kMinPaymentInterval);

    static constexpr std::uint32_t kDefaultGracePeriod = 60;
    static_assert(kDefaultGracePeriod >= kMinPaymentInterval);
};

//------------------------------------------------------------------------------

}  // namespace xrpl
