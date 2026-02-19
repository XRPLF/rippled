#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class NFTokenAcceptOffer : public Transactor
{
private:
    TER
    pay(AccountID const& from, AccountID const& to, STAmount const& amount);

    TER
    acceptOffer(std::shared_ptr<SLE> const& offer);

    TER
    bridgeOffers(std::shared_ptr<SLE> const& buy, std::shared_ptr<SLE> const& sell);

    TER
    transferNFToken(AccountID const& buyer, AccountID const& seller, uint256 const& nfTokenID);

public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit NFTokenAcceptOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
