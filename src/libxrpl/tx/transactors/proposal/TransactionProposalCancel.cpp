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
    // hash), so reaching here with zero is an internal error (and a mathematical rarity).
    if (proposalID == beast::kZero)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleProposal = ctx.view.read(keylet::txProposal(proposalID));
    if (!sleProposal)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCancel: proposal does not exist.";
        return tecNO_ENTRY;
    }

    // A live proposal may be cancelled only by its Owner or its target; once
    // it is terminal anyone may delete it and release the Owner's reserve
    // (XLS-0103 §7.2). See proposal::mayCancel for who counts as the target
    // and why targets may always refuse. Being terminal does not stop an
    // already signed payload from executing; see proposal::isTerminal.
    AccountID const account = ctx.tx[sfAccount];
    if (!proposal::mayCancel(ctx.view, *sleProposal, account))
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
TransactionProposalCancel::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
