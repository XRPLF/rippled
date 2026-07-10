#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>

namespace xrpl {

class Batch : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

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

    void
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    static constexpr auto kDisabledTxTypes = std::to_array<TxType>({
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

private:
    // Skips signature verification for inner txns, so keep it private: it must
    // only be reached through Batch::checkSign.
    static NotTEC
    checkBatchSign(PreclaimContext const& ctx);
};

}  // namespace xrpl
