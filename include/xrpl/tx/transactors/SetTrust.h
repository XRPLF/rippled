#pragma once

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class SetTrust : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit SetTrust(ApplyContext& ctx) : Transactor(ctx)
    {
    }

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

using TrustSet = SetTrust;

}  // namespace xrpl
