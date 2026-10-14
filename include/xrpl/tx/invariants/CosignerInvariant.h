#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace xrpl {

class ReadView;

/**
 * @brief Invariants for TransactionProposal ledger entries.
 *
 * - A proposal's payload and metadata are immutable except for signature
 *   fields and PreviousTxn bookkeeping.
 * - Proposal reserve changes match proposal creation and deletion.
 * - Only transaction types implementing proposal lifecycle operations may
 *   create or modify proposals.
 * - A proposal is deleted only by TransactionProposalCancel or together with
 *   the Ticket it is keyed to, and a deleted Ticket takes the proposal keyed
 *   to it along.
 * - A Cancel deletes exactly the proposal it names, and deletes a live one
 *   only for its Owner or its target.
 * - A Create, a Cancel, or a SponsorshipTransfer reassigning a proposal
 *   consumes no Ticket but the one it pays with, and creates none.
 * - Collected signer arrays remain sorted, unique, and bounded.
 */
class ValidTransactionProposal
{
    struct Change
    {
        bool isDelete;
        SLE::const_pointer before;
        SLE::const_pointer after;
    };

    std::vector<Change> changes_;
    // Before-state of the last TransactionProposal deleted; finalize reads it
    // only once it has checked that exactly one was.
    SLE::const_pointer deletedProposal_;
    // The proposal each deleted Ticket was keyed to, whether or not one
    // existed; none may survive its Ticket.
    std::vector<Keylet> proposalsOfDeletedTickets_;
    std::map<AccountID, std::int64_t> ownerCountDelta_;
    std::map<AccountID, std::int64_t> expectedOwnerCountDelta_;
    std::set<uint256> deletedTickets_;
    std::uint32_t createdTickets_ = 0;
    std::uint32_t created_ = 0;
    std::uint32_t modified_ = 0;
    std::uint32_t deleted_ = 0;
    bool invalidSignerArrays_ = false;

public:
    void
    visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after);

    [[nodiscard]] bool
    finalize(STTx const& tx, TER result, XRPAmount, ReadView const& view, beast::Journal const& j)
        const;
};

}  // namespace xrpl
