#ifndef XRPL_TX_BATCH_H_INCLUDED
#define XRPL_TX_BATCH_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Indexes.h>

namespace ripple {

class Batch : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit Batch(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    preflightSigValidated(PreflightContext const& ctx);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
