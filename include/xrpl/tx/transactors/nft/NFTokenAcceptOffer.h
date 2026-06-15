#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class NFTokenAcceptOffer : public Transactor
{
private:
    TER
    pay(AccountID const& from, AccountID const& to, STAmount const& amount);

    TER
    acceptOffer(SLE::ref offer);

    TER
    bridgeOffers(SLE::ref buy, SLE::ref sell);

    TER
    transferNFToken(AccountID const& buyer, AccountID const& seller, uint256 const& nfTokenID);

public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit NFTokenAcceptOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

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
};

}  // namespace xrpl
