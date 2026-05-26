#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class Change : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit Change(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    TER
    doApply() override;
    void
    preCompute() override;

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

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx)
    {
        return XRPAmount{0};
    }

    static TER
    preclaim(PreclaimContext const& ctx);

private:
    TER
    applyAmendment();

    TER
    applyFee();

    TER
    applyUNLModify();
};

using EnableAmendment = Change;
using SetFee = Change;
using UNLModify = Change;

}  // namespace xrpl
