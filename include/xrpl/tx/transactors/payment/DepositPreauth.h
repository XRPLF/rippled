#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

class DepositPreauth : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit DepositPreauth(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

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

    // Interface used by AccountDelete
    static TER
    removeFromLedger(ApplyView& view, uint256 const& delIndex, beast::Journal j);

private:
    /**
     * Number of DepositPreauth grants this transaction inserted.
     */
    std::uint32_t grantsCreated_{0};

    /**
     * Number of DepositPreauth grants this transaction erased.
     */
    std::uint32_t grantsRemoved_{0};

    /**
     * Set when a touched grant's owner is not the submitting account.
     */
    bool grantOwnerMismatch_{false};

    /**
     * Set when a created grant's credential array is not canonically sorted.
     */
    bool grantCredsNotCanonical_{false};
};

}  // namespace xrpl
