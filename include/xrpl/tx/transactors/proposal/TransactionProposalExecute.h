#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Applies the proposed transaction of a TransactionProposal.
 *
 * Modeled on Batch. checkSign runs the proposed transaction's preflight (as
 * Batch::preflight does for its inners; it cannot run in preflight, because
 * this transaction carries only the ProposalID) and then re-checks every
 * recorded approval against the current ledger (as Batch::checkSign does for
 * BatchSigners). doApply does nothing. applyTransaction then applies the
 * proposed transaction as an inner transaction, and, if that is a Batch, the
 * Batch's own inner transactions.
 */
class TransactionProposalExecute : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit TransactionProposalExecute(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
