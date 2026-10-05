#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>

namespace xrpl {

class MPTokenIssuanceSet : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit MPTokenIssuanceSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    // Maps each MPTokenIssuanceSet set flag(e.g., tfMPTSetCanLock), to the issuance's
    // corresponding immutable flag (e.g., lsifMPTCanLock) and the target ledger flag (e.g.,
    // lsfMPTCanLock).
    struct FlagMapping
    {
        std::uint32_t setFlag;
        std::uint32_t immutableFlag;
        std::uint32_t ledgerFlag;
    };

    static constexpr std::array<FlagMapping, 7> flagMapping = {
        {{.setFlag = tfMPTSetCanLock, .immutableFlag = lsifMPTCanLock, .ledgerFlag = lsfMPTCanLock},
         {.setFlag = tfMPTSetRequireAuth,
          .immutableFlag = lsifMPTRequireAuth,
          .ledgerFlag = lsfMPTRequireAuth},
         {.setFlag = tfMPTSetCanEscrow,
          .immutableFlag = lsifMPTCanEscrow,
          .ledgerFlag = lsfMPTCanEscrow},
         {.setFlag = tfMPTSetCanTrade,
          .immutableFlag = lsifMPTCanTrade,
          .ledgerFlag = lsfMPTCanTrade},
         {.setFlag = tfMPTSetCanTransfer,
          .immutableFlag = lsifMPTCanTransfer,
          .ledgerFlag = lsfMPTCanTransfer},
         {.setFlag = tfMPTSetCanClawback,
          .immutableFlag = lsifMPTCanClawback,
          .ledgerFlag = lsfMPTCanClawback},
         {.setFlag = tfMPTSetCanHoldConfidentialBalance,
          .immutableFlag = lsifMPTCanHoldConfidentialBalance,
          .ledgerFlag = lsfMPTCanHoldConfidentialBalance}}};

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
