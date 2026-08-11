#include <xrpl/tx/transactors/proposal/TransactionProposalCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

namespace xrpl {

NotTEC
TransactionProposalCancel::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfProposalID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCancel: zero ProposalID.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
TransactionProposalCancel::preclaim(PreclaimContext const& ctx)
{
    auto const sleProposal = ctx.view.read(keylet::txProposal(ctx.tx[sfProposalID]));
    if (!sleProposal)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCancel: proposal does not exist.";
        return tecNO_ENTRY;
    }

    STObject const proposedTx = sleProposal->getFieldObject(sfProposedTransaction);

    // A terminal proposal can never complete, so anyone may delete it and
    // release the Owner's reserve (XLS-0103 §7.2).
    if (proposal::isTerminal(ctx.view, (*sleProposal)[~sfExpiration], proposedTx))
        return tesSUCCESS;

    // A live proposal may only be cancelled by its Owner (the proposer) or
    // by its target. The target is the account the proposed transaction
    // would execute against (its Account field) or the Delegate named in it.
    // Targets may always refuse a proposal, because anyone can create one
    // against any account and doing so ties up one of that account's
    // tickets (XLS-0103 §7.2).
    //
    // The Delegate is read from the stored payload as-is; whether that
    // delegation exists on-ledger is not checked. The proposal asks that
    // account to sign, so that account may refuse — and refusing only
    // deletes the proposer's own entry and returns the proposer's own
    // reserve.
    AccountID const account = ctx.tx[sfAccount];
    if (account == sleProposal->getAccountID(sfOwner) ||
        account == proposedTx.getAccountID(sfAccount) || account == proposedTx[~sfDelegate])
        return tesSUCCESS;

    JLOG(ctx.j.debug()) << "TransactionProposalCancel: proposal is live and "
                           "canceller is neither its Owner nor its target.";
    return tecNO_PERMISSION;
}

TER
TransactionProposalCancel::doApply()
{
    // The below condition covers the case of TransactionProposalCancel
    // consuming the very ticket specified in the
    // sleProposal[sfProposedTransaction] field. This indicates the intent of
    // the Target-Account to cancel the ticket+associated-txProposal.
    Keylet const proposalKeylet = keylet::txProposal(ctx_.tx[sfProposalID]);
    auto const sleProposal = view().peek(proposalKeylet);
    if (!sleProposal)
    {
        SeqProxy const seqProx = ctx_.tx.getSeqProxy();
        if (seqProx.isTicket() &&
            keylet::txProposal(accountID_, seqProx.value()).key == proposalKeylet.key)
            return tesSUCCESS;

        return tefINTERNAL;  // LCOV_EXCL_LINE
    }

    return proposal::deleteProposal(view(), sleProposal, ctx_.registry.get().getJournal("View"));
}

void
TransactionProposalCancel::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
TransactionProposalCancel::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
