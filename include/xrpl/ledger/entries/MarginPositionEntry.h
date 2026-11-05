#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class MarginPositionEntry : public SLEBase<ViewT, ltMARGIN_POSITION>
{
public:
    using Base = SLEBase<ViewT, ltMARGIN_POSITION>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MarginPositionEntry(
        UInt256 const& marginAccountID,
        UInt256 const& marketID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::marginPosition(marginAccountID, marketID), view, j)
    {
    }

    explicit MarginPositionEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::marginPosition(key), view, j)
    {
    }
};

using MarginPositionEntryR = MarginPositionEntry<ReadView>;
using MarginPositionEntryW = MarginPositionEntry<ApplyView>;

}  // namespace xrpl
