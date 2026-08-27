#include <xrpl/tx/transactors/proposal/TransactionProposalCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

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
    uint256 const proposalID = ctx.tx[sfProposalID];

    // Never read the zero keylet. Preflight already rejected a zero
    // ProposalID, and no real proposal can have one (every ProposalID is a
    // hash), so reaching here with zero is an internal error. (and a mathematical rarity. )
    if (proposalID == beast::kZero)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleProposal = ctx.view.read(keylet::txProposal(proposalID));
    if (!sleProposal)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCancel: proposal does not exist.";
        return tecNO_ENTRY;
    }

    STObject const proposedTx = sleProposal->getFieldObject(sfProposedTransaction);

    // A terminal proposal can never complete, so anyone may delete it and
    // release the Owner's reserve (On-Chain Cosigner spec §7.2).
    bool const terminal = proposal::isTerminal(ctx.view, (*sleProposal)[~sfExpiration], proposedTx);

    // A live proposal may only be cancelled by its Owner (the proposer) or
    // by its target. The target is the account the proposed transaction
    // would execute against (its Account field) or the Delegate named in it.
    // Targets may always refuse a proposal, because anyone can create one
    // against any account and doing so ties up one of that account's
    // tickets (On-Chain Cosigner spec §7.2).
    //
    // The Delegate is read from the stored payload as-is; whether that
    // delegation exists on-ledger is not checked. The proposal asks that
    // account to sign, so that account may refuse — and refusing only
    // deletes the proposer's own entry and returns the proposer's own
    // reserve.
    AccountID const account = ctx.tx[sfAccount];
    bool const ownerOrTarget = account == sleProposal->getAccountID(sfOwner) ||
        account == proposedTx.getAccountID(sfAccount) || account == proposedTx[~sfDelegate];

    if (!terminal && !ownerOrTarget)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCancel: proposal is live and "
                               "canceller is neither its Owner nor its target.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
TransactionProposalCancel::doApply()
{
    uint256 const proposalID = ctx_.tx[sfProposalID];

    // Never read the zero keylet; see the matching guard in preclaim.
    if (proposalID == beast::kZero)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleProposal = view().peek(keylet::txProposal(proposalID));
    if (!sleProposal)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    return proposal::deleteProposal(view(), sleProposal, ctx_.registry.get().getJournal("View"));
}

void
TransactionProposalCancel::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work). Unreachable while
    // Transactor::checkInvariants skips transaction-specific invariant dispatch
    // (disabled for 3.2.0, to be re-enabled for 3.3.0; see PR #7409).
}

bool
TransactionProposalCancel::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work). Unreachable while
    // Transactor::checkInvariants skips transaction-specific invariant dispatch
    // (disabled for 3.2.0, to be re-enabled for 3.3.0; see PR #7409).
    return true;
}

}  // namespace xrpl
