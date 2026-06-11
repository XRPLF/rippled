#include <xrpl/tx/transactors/system/TicketCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <cstdint>
#include <memory>

namespace xrpl {

TxConsequences
TicketCreate::makeTxConsequences(PreflightContext const& ctx)
{
    // Create TxConsequences identifying the number of sequences consumed.
    return TxConsequences{ctx.tx, ctx.tx[sfTicketCount]};
}

NotTEC
TicketCreate::preflight(PreflightContext const& ctx)
{
    if (std::uint32_t const count = ctx.tx[sfTicketCount];
        count < kMinValidCount || count > kMaxValidCount)
        return temINVALID_COUNT;

    return tesSUCCESS;
}

TER
TicketCreate::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];
    auto const sleAccountRoot = ctx.view.read(keylet::account(id));
    if (!sleAccountRoot)
        return terNO_ACCOUNT;

    // Make sure the TicketCreate would not cause the account to own
    // too many tickets.
    std::uint32_t const curTicketCount = (*sleAccountRoot)[~sfTicketCount].value_or(0u);
    std::uint32_t const addedTickets = ctx.tx[sfTicketCount];
    std::uint32_t const consumedTickets = ctx.tx.getSeqProxy().isTicket() ? 1u : 0u;

    // Note that unsigned integer underflow can't currently happen because
    //  o curTicketCount   >= 0
    //  o addedTickets     >= 1
    //  o consumedTickets  <= 1
    // So in the worst case addedTickets == consumedTickets and the
    // computation yields curTicketCount.
    if (curTicketCount + addedTickets - consumedTickets > kMaxTicketThreshold)
        return tecDIR_FULL;

    return tesSUCCESS;
}

TER
TicketCreate::doApply()
{
    SLE::pointer const sleAccountRoot = view().peek(keylet::account(accountID_));
    if (!sleAccountRoot)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Each ticket counts against the reserve of the issuing account, but we
    // check the starting balance because we want to allow dipping into the
    // reserve to pay fees.
    std::uint32_t const ticketCount = ctx_.tx[sfTicketCount];
    {
        XRPAmount const reserve =
            view().fees().accountReserve(sleAccountRoot->getFieldU32(sfOwnerCount) + ticketCount);

        if (preFeeBalance_ < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    beast::Journal const viewJ{ctx_.registry.get().getJournal("View")};

    // The starting ticket sequence is the same as the current account
    // root sequence.  Before we got here to doApply(), the transaction
    // machinery already incremented the account root sequence if that
    // was appropriate.
    std::uint32_t const firstTicketSeq = (*sleAccountRoot)[sfSequence];

    // Sanity check that the transaction machinery really did already
    // increment the account root Sequence.
    if (std::uint32_t const txSeq = ctx_.tx[sfSequence];
        txSeq != 0 && txSeq != (firstTicketSeq - 1))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    for (std::uint32_t i = 0; i < ticketCount; ++i)
    {
        std::uint32_t const curTicketSeq = firstTicketSeq + i;
        Keylet const ticketKeylet = keylet::kTicket(accountID_, curTicketSeq);
        SLE::pointer const sleTicket = std::make_shared<SLE>(ticketKeylet);

        sleTicket->setAccountID(sfAccount, accountID_);
        sleTicket->setFieldU32(sfTicketSequence, curTicketSeq);
        view().insert(sleTicket);

        auto const page = view().dirInsert(
            keylet::ownerDir(accountID_), ticketKeylet, describeOwnerDir(accountID_));

        JLOG(j_.trace()) << "Creating ticket " << to_string(ticketKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        sleTicket->setFieldU64(sfOwnerNode, *page);
    }

    // Update the record of the number of Tickets this account owns.
    std::uint32_t const oldTicketCount = (*(sleAccountRoot))[~sfTicketCount].valueOr(0u);

    sleAccountRoot->setFieldU32(sfTicketCount, oldTicketCount + ticketCount);

    // Every added Ticket counts against the creator's reserve.
    adjustOwnerCount(view(), sleAccountRoot, ticketCount, viewJ);

    // TicketCreate is the only transaction that can cause an account root's
    // Sequence field to increase by more than one.  October 2018.
    sleAccountRoot->setFieldU32(sfSequence, firstTicketSeq + ticketCount);

    return tesSUCCESS;
}

void
TicketCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
TicketCreate::finalizeInvariants(
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
