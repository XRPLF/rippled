#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class ContractDelete : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ContractDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    // Interface used by DeleteAccount
    static TER
    deleteContract(
        ApplyView& view,
        std::shared_ptr<SLE> const& sle,
        AccountID const& account,
        beast::Journal j);

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
