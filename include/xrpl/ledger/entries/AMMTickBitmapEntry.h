#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class AMMTickBitmapEntry : public SLEBase<ViewT, ltAMM_TICK_BITMAP>
{
public:
    using Base = SLEBase<ViewT, ltAMM_TICK_BITMAP>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMTickBitmapEntry(
        UInt256 const& ammID,
        std::uint16_t wordIndex,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammTickBitmapWord(ammID, wordIndex), view, j)
    {
    }

    explicit AMMTickBitmapEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammTickBitmapWord(key), view, j)
    {
    }
};

using AMMTickBitmapEntryR = AMMTickBitmapEntry<ReadView>;
using AMMTickBitmapEntryW = AMMTickBitmapEntry<ApplyView>;

}  // namespace xrpl
