#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>

namespace xrpl {

class SponsorshipSet : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Custom;

    explicit SponsorshipSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

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

    /**
     * Create a new Sponsorship object between the sponsor and sponsee.
     *
     * Moves feeAmount from the sponsor's balance into the object, checks the
     * sponsor's reserve (including the new object) and links the object into
     * both owner directories.
     *
     * @param ctx The apply-view context (view + tx)
     * @param sponsorAccSle The sponsor's account root
     * @param sponseeID The sponsee account
     * @param reserveSponsorAccSle The reserve sponsor of the new object (if any)
     * @param feeAmount Positive XRP amount to prefund, if any
     * @param maxFee Positive XRP per-transaction fee cap, if any
     * @param remainingOwnerCount Positive owner count budget, if any
     * @param flags Ledger flags of the new object
     * @param j Journal for logging
     * @return Transaction result code
     */
    [[nodiscard]] static TER
    createSponsorship(
        ApplyViewContext ctx,
        SLE::Ref sponsorAccSle,
        AccountID const& sponseeID,
        SLE::Ref reserveSponsorAccSle,
        std::optional<STAmount> const& feeAmount,
        std::optional<STAmount> const& maxFee,
        std::optional<std::uint32_t> const& remainingOwnerCount,
        std::uint32_t flags,
        beast::Journal j);
};

}  // namespace xrpl
