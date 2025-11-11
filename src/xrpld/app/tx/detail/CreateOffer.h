#ifndef XRPL_TX_CREATEOFFER_H_INCLUDED
#define XRPL_TX_CREATEOFFER_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/protocol/Quality.h>

namespace ripple {

class PaymentSandbox;
class Sandbox;

/** Transactor specialized for creating offers in the ledger. */
class CreateOffer : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    /** Construct a Transactor subclass that creates an offer in the ledger. */
    explicit CreateOffer(ApplyContext& ctx) : Transactor(ctx)
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

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view, Sandbox& view_cancel);

    // Determine if we are authorized to hold the asset we want to get.
    static TER
    checkAcceptAsset(
        ReadView const& view,
        ApplyFlags const flags,
        AccountID const id,
        beast::Journal const j,
        Issue const& issue);

    // Use the payment flow code to perform offer crossing.
    std::pair<TER, Amounts>
    flowCross(
        PaymentSandbox& psb,
        PaymentSandbox& psbCancel,
        Amounts const& takerAmount,
        std::optional<uint256> const& domainID);

    static std::string
    format_amount(STAmount const& amount);

    TER
    applyHybrid(
        Sandbox& sb,
        std::shared_ptr<STLedgerEntry> sleOffer,
        Keylet const& offer_index,
        STAmount const& saTakerPays,
        STAmount const& saTakerGets,
        std::function<void(SLE::ref, std::optional<uint256>)> const& setDir);
};

using OfferCreate = CreateOffer;

}  // namespace ripple

#endif
