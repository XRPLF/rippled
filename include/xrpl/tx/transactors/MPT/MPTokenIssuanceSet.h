#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class MPTokenIssuanceSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit MPTokenIssuanceSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkDelegatePermission(
        ReadView const& view,
        STTx const& tx,
        std::shared_ptr<SLE const> const& sle,
        std::unordered_set<GranularPermissionType> const& granularPermissions);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
