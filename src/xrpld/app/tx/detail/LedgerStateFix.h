#ifndef XRPL_TX_LEDGER_STATE_FIX_H_INCLUDED
#define XRPL_TX_LEDGER_STATE_FIX_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

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

}  // namespace ripple

#endif
