#pragma once

#include <xrpl/protocol/Quality.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class PaymentSandbox;
class Sandbox;

/** Transactor specialized for creating offers in the ledger. */
class OfferCreate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Custom;

    /** Construct a Transactor subclass that creates an offer in the ledger. */
    explicit OfferCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Precondition: fee collection is likely.  Attempt to create the offer. */
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

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view, Sandbox& viewCancel);

    // Determine if we are authorized to hold the asset we want to get.
    static TER
    checkAcceptAsset(
        ReadView const& view,
        ApplyFlags const flags,
        AccountID const id,
        beast::Journal const j,
        Asset const& asset);

    // Use the payment flow code to perform offer crossing.
    std::pair<TER, Amounts>
    flowCross(
        PaymentSandbox& psb,
        PaymentSandbox& psbCancel,
        Amounts const& takerAmount,
        std::optional<uint256> const& domainID);

    static std::string
    formatAmount(STAmount const& amount);

    TER
    applyHybrid(
        Sandbox& sb,
        STLedgerEntry::pointer sleOffer,
        Keylet const& offerIndex,
        STAmount const& saTakerPays,
        STAmount const& saTakerGets,
        std::uint64_t openRate,
        std::function<void(SLE::ref, std::optional<uint256>)> const& setDir);
};

}  // namespace xrpl
