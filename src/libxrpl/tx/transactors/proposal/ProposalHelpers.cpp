#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl::proposal {

bool
isTerminal(
    ReadView const& view,
    std::optional<std::uint32_t> expiration,
    STObject const& proposedTx)
{
    if (hasExpired(view, expiration))
        return true;

    return proposedTx.isFieldPresent(sfLastLedgerSequence) &&
        view.seq() >= proposedTx.getFieldU32(sfLastLedgerSequence);
}

TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j)
{
    // view carries no null contract (a reference), but the two parameters are
    // bound to each other: sleProposal must be a live entry of this same
    // view, since the directory removal, owner-root peek, and erase below all
    // mutate that view assuming they see the entry's state.
    XRPL_ASSERT(
        sleProposal && sleProposal->getType() == ltTRANSACTION_PROPOSAL &&
            view.exists(Keylet{ltTRANSACTION_PROPOSAL, sleProposal->key()}),
        "xrpl::proposal::deleteProposal : valid proposal sle of this view");

    AccountID const owner = sleProposal->getAccountID(sfOwner);

    std::uint64_t const page{(*sleProposal)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(owner), page, sleProposal->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete TransactionProposal from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Could not find TransactionProposal owner account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Release the reserve against the Owner or, if the entry carries a
    // reserve sponsor, against that sponsor.
    decreaseOwnerCountForObject(
        view,
        sleOwner,
        sleProposal,
        proposalOwnerCount(sleProposal->getFieldObject(sfProposedTransaction)),
        j);

    view.erase(sleProposal);
    return tesSUCCESS;
}

}  // namespace xrpl::proposal
