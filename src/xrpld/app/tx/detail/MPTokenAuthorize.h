#ifndef XRPL_TX_MPTOKENAUTHORIZE_H_INCLUDED
#define XRPL_TX_MPTOKENAUTHORIZE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

struct MPTAuthorizeArgs
{
    XRPAmount const& priorBalance;
    MPTID const& mptIssuanceID;
    AccountID const& account;
    std::uint32_t flags{};
    std::optional<AccountID> holderID{};
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

    static TER
    createMPToken(
        ApplyView& view,
        MPTID const& mptIssuanceID,
        AccountID const& account,
        std::uint32_t const flags);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
