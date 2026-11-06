#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class TransactionProposalEntry : public SLEBase<ViewT, ltTRANSACTION_PROPOSAL>
{
public:
    using Base = SLEBase<ViewT, ltTRANSACTION_PROPOSAL>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit TransactionProposalEntry(
        AccountID const& target,
        std::uint32_t ticketSequence,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::txProposal(target, ticketSequence), view, j)
    {
    }
};

using TransactionProposalEntryR = TransactionProposalEntry<ReadView>;
using TransactionProposalEntryW = TransactionProposalEntry<ApplyView>;

}  // namespace xrpl
