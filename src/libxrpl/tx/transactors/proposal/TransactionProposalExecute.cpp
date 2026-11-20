#include <xrpl/tx/transactors/proposal/TransactionProposalExecute.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <exception>

namespace xrpl {

NotTEC
TransactionProposalExecute::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfProposalID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalExecute: zero ProposalID.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

XRPAmount
TransactionProposalExecute::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The executor pays for itself and for the proposed transaction, whose own
    // Fee is 0. The proposed transaction's base fee already scales with the
    // approvals it holds: one base fee per Signers entry, and for a Batch one
    // per BatchSigners entry plus each inner.
    XRPAmount const baseFee = Transactor::calculateBaseFee(view, tx);

    auto const sleProposal = view.read(keylet::txProposal(tx[sfProposalID]));
    if (!sleProposal)
        return baseFee;

    try
    {
        STTx const proposedTx{STObject{sleProposal->getFieldObject(sfProposedTransaction)}};
        if (auto const proposedFee = xrpl::calculateBaseFee(view, proposedTx))
            return baseFee + *proposedFee;
    }
    catch (std::exception const&)  // LCOV_EXCL_LINE
    {
    }
    return baseFee;  // LCOV_EXCL_LINE
}

namespace {

// The proposal Execute names, if it exists, is live, and holds a transaction
// marked as proposed. Otherwise preclaim reports why.
SLE::const_pointer
executableProposal(ReadView const& view, STTx const& tx)
{
    auto sleProposal = view.read(keylet::txProposal(tx[sfProposalID]));
    if (!sleProposal)
        return nullptr;

    STObject const proposedTx = sleProposal->getFieldObject(sfProposedTransaction);
    if (proposal::isTerminal(view, (*sleProposal)[~sfExpiration], proposedTx) ||
        (proposedTx.getFieldU32(sfFlags) & tfProposedTxn) == 0u)
        return nullptr;

    return sleProposal;
}

// A failure of the proposed transaction's own checks means it is not
// authorized now: an approval is missing, stale, or the payload can no longer
// run. The tef codes from the key checks already say which; a tem from the
// proposed transaction does not describe this transaction, so it is reported
// as tefBAD_AUTH.
NotTEC
asAuthFailure(NotTEC ter)
{
    if (isTesSuccess(ter) || isTefFailure(ter) || isTerRetry(ter))
        return ter;
    return tefBAD_AUTH;
}

}  // namespace

NotTEC
TransactionProposalExecute::checkSign(PreclaimContext const& ctx)
{
    // The executor's own signature.
    if (auto const ret = Transactor::checkSign(ctx))
        return ret;

    auto const sleProposal = executableProposal(ctx.view, ctx.tx);
    if (!sleProposal)
        return tesSUCCESS;

    try
    {
        STTx const proposedTx{STObject{sleProposal->getFieldObject(sfProposedTransaction)}};

        // Stateless checks of the payload, as Batch::preflight runs on its
        // inners. TapProposalExecute skips only the cryptographic step and
        // requires every slot to hold an approval. For a Batch this also
        // checks that every participant has a BatchSigners entry.
        auto const pf =
            xrpl::preflight(ctx.registry, ctx.view.rules(), proposedTx, TapProposalExecute, ctx.j);
        if (!isTesSuccess(pf.ter))
        {
            JLOG(ctx.j.debug()) << "TransactionProposalExecute: proposed txn preflight: "
                                << transToken(pf.ter);
            return asAuthFailure(pf.ter);
        }

        // Each recorded key must be authorized for its account by the current
        // ledger: master key not disabled, current RegularKey, or SignerList
        // membership with quorum. This is the type's own checkSign, as
        // Batch::checkSign checks its BatchSigners.
        // Every transactor that overrides checkSign must be listed here.
        PreclaimContext const proposedCtx(
            ctx.registry, ctx.view, tesSUCCESS, proposedTx, TapProposalExecute, ctx.j);
        NotTEC const ter = [&] {
            switch (proposedTx.getTxnType())
            {
                case ttBATCH:
                    return Batch::checkSign(proposedCtx);
                case ttLOAN_SET:
                    return LoanSet::checkSign(proposedCtx);
                default:
                    return Transactor::checkSign(proposedCtx);
            }
        }();
        if (!isTesSuccess(ter))
        {
            JLOG(ctx.j.debug()) << "TransactionProposalExecute: proposed txn not authorized: "
                                << transToken(ter);
        }
        return asAuthFailure(ter);
    }
    catch (std::exception const& e)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalExecute: proposed txn is malformed: "
                            << e.what();
        return tefBAD_AUTH;
    }
}

TER
TransactionProposalExecute::preclaim(PreclaimContext const& ctx)
{
    auto const sleProposal = ctx.view.read(keylet::txProposal(ctx.tx[sfProposalID]));
    if (!sleProposal)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalExecute: proposal does not exist.";
        return tecNO_ENTRY;
    }

    STObject const proposedTx = sleProposal->getFieldObject(sfProposedTransaction);

    // A terminal proposal cannot be executed (approval model §4.5).
    // TODO: delete it as a side effect, as the spec asks. That needs
    // ltTRANSACTION_PROPOSAL on the tecEXPIRED deletion allow-list.
    if (proposal::isTerminal(ctx.view, (*sleProposal)[~sfExpiration], proposedTx))
        return tecEXPIRED;

    // Only a transaction marked at creation as proposed may run this way.
    if ((proposedTx.getFieldU32(sfFlags) & tfProposedTxn) == 0u)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalExecute: proposed txn lacks tfProposedTxn.";
        return tecNO_PERMISSION;
    }

    // The proposed transaction's own preclaim runs when it is applied, as a
    // Batch inner's does.
    return tesSUCCESS;
}

TER
TransactionProposalExecute::doApply()
{
    // The proposed transaction is applied after this one, by applyTransaction,
    // in the same way a Batch's inner transactions are. Spending its Ticket
    // deletes the proposal.
    return tesSUCCESS;
}

void
TransactionProposalExecute::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
TransactionProposalExecute::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
