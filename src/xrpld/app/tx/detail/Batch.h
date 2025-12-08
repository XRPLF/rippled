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

    static constexpr auto disabledTxTypes = std::to_array<TxType>({
        ttVAULT_CREATE,
        ttVAULT_SET,
        ttVAULT_DELETE,
        ttVAULT_DEPOSIT,
        ttVAULT_WITHDRAW,
        ttVAULT_CLAWBACK,
        ttLOAN_BROKER_SET,
        ttLOAN_BROKER_DELETE,
        ttLOAN_BROKER_COVER_DEPOSIT,
        ttLOAN_BROKER_COVER_WITHDRAW,
        ttLOAN_BROKER_COVER_CLAWBACK,
        ttLOAN_SET,
        ttLOAN_DELETE,
        ttLOAN_MANAGE,
        ttLOAN_PAY,
    });
};

}  // namespace ripple

#endif
