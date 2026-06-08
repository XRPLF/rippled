#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class Payment : public Transactor
{
    /* The largest number of paths we allow */
    static std::size_t const kMaxPathSize = 6;

    /* The longest path we allow */
    static std::size_t const kMaxPathLength = 8;

public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Custom;

    explicit Payment(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkGranularSemantics(
        ReadView const& view,
        STTx const& tx,
        std::unordered_set<GranularPermissionType> const& heldGranularPermissions);

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
