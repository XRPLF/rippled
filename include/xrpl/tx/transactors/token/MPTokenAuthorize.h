#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

struct MPTAuthorizeArgs
{
    XRPAmount const& priorBalance;
    MPTID const& mptIssuanceID;
    AccountID const& account;
    std::uint32_t flags{};
    std::optional<AccountID> holderID;
};

class MPTokenAuthorize : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit MPTokenAuthorize(ApplyContext& ctx) : Transactor(ctx)
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
