#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
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
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

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
