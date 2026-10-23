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
class BallotEntry : public SLEBase<ViewT, ltBALLOT>
{
public:
    using Base = SLEBase<ViewT, ltBALLOT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit BallotEntry(
        AccountID const& owner,
        std::uint32_t seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ballot(owner, seq), view, j)
    {
    }
};

using BallotEntryR = BallotEntry<ReadView>;
using BallotEntryW = BallotEntry<ApplyView>;

}  // namespace xrpl
