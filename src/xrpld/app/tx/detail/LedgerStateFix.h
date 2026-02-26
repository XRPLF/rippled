#pragma once

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

class LedgerStateFix : public Transactor
{
public:
    enum FixType : std::uint16_t {
        nfTokenPageLink = 1,
    };

    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

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
};

}  // namespace xrpl
