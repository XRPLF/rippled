#pragma once

#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class LoanSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

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
    static std::uint32_t constexpr kMIN_PAYMENT_TOTAL = 1;
    static std::uint32_t constexpr kDEFAULT_PAYMENT_TOTAL = 1;
    static_assert(kDEFAULT_PAYMENT_TOTAL >= kMIN_PAYMENT_TOTAL);

    static std::uint32_t constexpr kMIN_PAYMENT_INTERVAL = 60;
    static std::uint32_t constexpr kDEFAULT_PAYMENT_INTERVAL = 60;
    static_assert(kDEFAULT_PAYMENT_INTERVAL >= kMIN_PAYMENT_INTERVAL);

    static std::uint32_t constexpr kDEFAULT_GRACE_PERIOD = 60;
    static_assert(kDEFAULT_GRACE_PERIOD >= kMIN_PAYMENT_INTERVAL);
};

//------------------------------------------------------------------------------

}  // namespace xrpl
