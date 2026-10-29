#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class BallotVoteEntry : public SLEBase<ViewT, ltBALLOT_VOTE>
{
public:
    using Base = SLEBase<ViewT, ltBALLOT_VOTE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit BallotVoteEntry(
        UInt256 const& ballotID,
        AccountID const& voter,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ballotVote(ballotID, voter), view, j)
    {
    }
};

using BallotVoteEntryR = BallotVoteEntry<ReadView>;
using BallotVoteEntryW = BallotVoteEntry<ApplyView>;

}  // namespace xrpl
