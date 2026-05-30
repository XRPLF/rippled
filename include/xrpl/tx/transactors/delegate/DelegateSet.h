#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class DelegateSet : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit DelegateSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    static AccessSet
    accessSetOf(STTx const& tx, ReadView const& base);

    TER
    doApply() override;

    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    // Interface used by AccountDelete
    static TER
    deleteDelegate(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);
};

}  // namespace xrpl
