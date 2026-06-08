#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class LedgerStateFix : public Transactor
{
public:
    enum class FixType : std::uint16_t {
        NfTokenPageLink = 1,
        BookExchangeRate = 2,
    };

    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit LedgerStateFix(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

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
};

}  // namespace xrpl
