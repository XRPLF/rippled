#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class SponsorshipSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit SponsorshipSet(ApplyContext& ctx) : Transactor(ctx)
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

    // Interface used by DeleteAccount
    static TER
    deleteSponsorship(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);
};

}  // namespace xrpl
