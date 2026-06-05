#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class DIDDelete : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit DIDDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner);

    static TER
    deleteSLE(ApplyView& view, SLE::pointer sle, AccountID const owner, beast::Journal j);

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
