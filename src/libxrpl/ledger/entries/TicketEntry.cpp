#include <xrpl/ledger/entries/TicketEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
TER
TicketEntry<ViewT>::removeFromLedger(AccountID const& owner)
    requires Base::kIsWritable
{
    beast::Journal const j = this->journal();

    // Delete the Ticket, adjust the account root ticket count, and
    // reduce the owner count.
    std::uint64_t const page{(**this)[sfOwnerNode]};
    if (!this->applyView().dirRemove(keylet::ownerDir(owner), page, this->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Ticket from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Update the account root's TicketCount.  If the ticket count drops to
    // zero remove the (optional) field.
    auto sleAccount = this->applyView().peek(keylet::account(owner));
    if (!sleAccount)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Could not find Ticket owner account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    if (auto ticketCount = (*sleAccount)[~sfTicketCount])
    {
        if (*ticketCount == 1)
        {
            sleAccount->makeFieldAbsent(sfTicketCount);
        }
        else
        {
            ticketCount = *ticketCount - 1;
        }
    }
    else
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "TicketCount field missing from account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Update the Ticket owner's reserve.
    decreaseOwnerCountForObject(this->applyView(), sleAccount, this->mutableRawSle(), 1, j);

    // Remove Ticket from ledger.
    this->erase();
    return tesSUCCESS;
}

template class TicketEntry<ReadView>;
template class TicketEntry<ApplyView>;

}  // namespace xrpl
